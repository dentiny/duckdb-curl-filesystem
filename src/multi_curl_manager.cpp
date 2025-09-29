#include "multi_curl_manager.hpp"

#include <array>
#include <curl/curl.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "syscall_macros.hpp"

#include <iostream>

#define CHECK_CURL_OK(expr)  D_ASSERT((expr) == CURLE_OK)
#define CHECK_CURLM_OK(expr) D_ASSERT((expr) == CURLM_OK)

namespace duckdb {

namespace {
struct SockInfo {
	curl_socket_t sockfd = 0;
	CURL *easy = nullptr;
	int action = 0;
	long timeout = 0;
	GlobalInfo *global = nullptr;
};

int MultiTimerCallback(CURLM *multi, long timeout_ms, struct GlobalInfo *global_info) {
	struct itimerspec its;

	if (timeout_ms > 0) {
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = timeout_ms / 1000;
		its.it_value.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
	} else if (timeout_ms == 0) {
		/* libcurl wants us to timeout now, however setting both fields of
		 * new_value.it_value to zero disarms the timer. The closest we can
		 * do is to schedule the timer to fire in 1 ns. */
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 0;
		its.it_value.tv_nsec = 1;
	} else {
		memset(&its, 0, sizeof(its));
	}

	int ret = timerfd_settime(global_info->timer_fd, /*flags=*/0, &its, NULL);
	SYSCALL_THROW_IF_ERROR(ret);

	return 0;
}

// Retrieve completed IO operations and invoke callback.
void CheckMultiWithNoLock(struct GlobalInfo *global_info) {
	std::lock_guard<std::mutex> lck(global_info->mu);

	CURLMsg *msg = nullptr;
	int msgs_left = 0;
	while ((msg = curl_multi_info_read(global_info->multi, &msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			CURL *easy = msg->easy_handle;
			EasyRequest *request = nullptr;
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &request);

			// Set response code.
			long response_code = 0;
			curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
			request->info->response_code = static_cast<uint16_t>(response_code);

			// Set http response.
			HTTPStatusCode code = HTTPUtil::ToStatusCode(request->info->response_code);
			auto resp = make_uniq<HTTPResponse>(std::move(code));
			resp->body = request->info->body;

			// Mark request completion.
			request->done.set_value(std::move(resp));
			request->completed.store(true);

			curl_multi_remove_handle(global_info->multi, easy);
			curl_easy_cleanup(easy);
		}
	}
}

void TimerCallback(struct GlobalInfo *global_info, int revents) {
	std::lock_guard<std::mutex> lck(global_info->mu);
	uint64_t count = 0;

	int ret = read(global_info->timer_fd, &count, sizeof(uint64_t));
	SYSCALL_THROW_IF_ERROR(ret);

	CURLMcode rc = curl_multi_socket_action(global_info->multi, CURL_SOCKET_TIMEOUT, 0, &global_info->still_running);
	CheckMultiWithNoLock(global_info);
}

void FsEventCallback(struct GlobalInfo *global_info, int fd, int revents) {
	std::lock_guard<std::mutex> lck(global_info->mu);
	int action = ((revents & EPOLLIN) ? CURL_CSELECT_IN : 0) | ((revents & EPOLLOUT) ? CURL_CSELECT_OUT : 0);
	CURLMcode rc = curl_multi_socket_action(global_info->multi, fd, action, &global_info->still_running);
	CheckMultiWithNoLock(global_info);
	if (global_info->still_running <= 0) {
		struct itimerspec its;
		memset(&its, 0, sizeof(its));
		int ret = timerfd_settime(global_info->timer_fd, 0, &its, NULL);
		SYSCALL_THROW_IF_ERROR(ret);
	}
}

void RemoveSockInfo(SockInfo *sock_info, GlobalInfo *global_info) {
	if (sock_info == nullptr) {
		return;
	}
	if (sock_info->sockfd == 0) {
		return;
	}
	const int ret = epoll_ctl(global_info->epoll_fd, EPOLL_CTL_DEL, sock_info->sockfd, NULL);
	SYSCALL_THROW_IF_ERROR(ret);
	delete sock_info;
}
void SetSockInfo(SockInfo *sock_info, curl_socket_t s, CURL *e, int act, GlobalInfo *global_info) {
	epoll_event ev;
	int kind = ((act & CURL_POLL_IN) ? EPOLLIN : 0) | ((act & CURL_POLL_OUT) ? EPOLLOUT : 0);
	if (sock_info->sockfd != 0) {
		const int ret = epoll_ctl(global_info->epoll_fd, EPOLL_CTL_DEL, sock_info->sockfd, NULL);
		SYSCALL_THROW_IF_ERROR(ret);
	}

	sock_info->sockfd = s;
	sock_info->action = act;
	sock_info->easy = e;

	ev.events = kind;
	ev.data.fd = s;
	const int ret = epoll_ctl(global_info->epoll_fd, EPOLL_CTL_ADD, s, &ev);
	SYSCALL_THROW_IF_ERROR(ret);
}
void AddSockInfo(curl_socket_t s, CURL *easy, int action, GlobalInfo *global_info) {
	struct SockInfo *sock_info = new SockInfo {};
	sock_info->global = global_info;
	SetSockInfo(sock_info, s, easy, action, global_info);
	curl_multi_assign(global_info->multi, s, sock_info);
}
int SocketCallback(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp) {
	auto *global_info = static_cast<GlobalInfo *>(cbp);
	auto *sock_info = static_cast<SockInfo *>(sockp);

	if (what == CURL_POLL_REMOVE) {
		RemoveSockInfo(sock_info, global_info);
		return 0;
	}
	if (sock_info == nullptr) {
		AddSockInfo(s, e, what, global_info);
	} else {
		SetSockInfo(sock_info, s, e, what, global_info);
	}
	return 0;
}
} // namespace

/*static*/ MultiCurlManager &MultiCurlManager::GetInstance() {
	static auto *multi_curl_manager = new MultiCurlManager();
	return *multi_curl_manager;
}

MultiCurlManager::MultiCurlManager() {
	// Initialize epoll and timer.
	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	SYSCALL_THROW_IF_ERROR(epoll_fd);

	int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	SYSCALL_THROW_IF_ERROR(timer_fd);

	struct itimerspec its;
	memset(&its, 0, sizeof(its));
	its.it_interval.tv_sec = 0;
	its.it_value.tv_sec = 1;
	int ret = timerfd_settime(timer_fd, 0, &its, NULL);
	SYSCALL_THROW_IF_ERROR(ret);

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = timer_fd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev);
	SYSCALL_THROW_IF_ERROR(ret);

	// Initialize multi-curl and global info.
	auto *multicurl = curl_multi_init();
	global_info.epoll_fd = epoll_fd;
	global_info.timer_fd = timer_fd;
	global_info.multi = multicurl;
	global_info.still_running = 0;

	CHECK_CURLM_OK(curl_multi_setopt(global_info.multi, CURLMOPT_SOCKETFUNCTION, SocketCallback));
	CHECK_CURLM_OK(curl_multi_setopt(global_info.multi, CURLMOPT_SOCKETDATA, &global_info));
	CHECK_CURLM_OK(curl_multi_setopt(global_info.multi, CURLMOPT_TIMERFUNCTION, MultiTimerCallback));
	CHECK_CURLM_OK(curl_multi_setopt(global_info.multi, CURLMOPT_TIMERDATA, &global_info));

	// Start a background thread to handle epoll events.
	bkg_thread = std::thread([this]() { HandleEvent(); });
}

void MultiCurlManager::HandleEvent() {
	std::array<epoll_event, 32> events {};

	// Run the event loop continuously
	while (true) {
		const int count =
		    epoll_wait(global_info.epoll_fd, events.data(), sizeof(events) / sizeof(epoll_event), /*timeout=*/-1);
		SYSCALL_EXIT_IF_ERROR(count);

		for (int idx = 0; idx < count; ++idx) {
			if (events[idx].data.fd == global_info.timer_fd) {
				TimerCallback(&global_info, events[idx].events);
			} else {
				FsEventCallback(&global_info, events[idx].data.fd, events[idx].events);
			}
		}
	}
}

unique_ptr<HTTPResponse> MultiCurlManager::HandleRequest(unique_ptr<EasyRequest> request) {
	auto fut = request->done.get_future();
	{
		std::lock_guard<std::mutex> lck(global_info.mu);
		curl_easy_setopt(request->easy, CURLOPT_PRIVATE, request.get());
		CHECK_CURLM_OK(curl_multi_add_handle(global_info.multi, request->easy));
	}
	return fut.get();
}

} // namespace duckdb
