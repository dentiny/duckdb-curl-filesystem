#include "multi_curl_manager.hpp"

#include <array>
#include <cstring>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "duckdb/common/helper.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "syscall_macros.hpp"

namespace duckdb {

namespace {
struct SockInfo {
	curl_socket_t sockfd = 0;
	CURL *easy = nullptr;
	int action = 0;
	GlobalInfo *global = nullptr;
};

void CheckMulti(GlobalInfo *g) {
	CURLMsg *msg = nullptr;
	int msgs_left = 0;

	while ((msg = curl_multi_info_read(g->multi, &msgs_left))) {
		if (msg->msg != CURLMSG_DONE) {
			continue;
		}
		CURL *easy = msg->easy_handle;
		CurlRequest *req = nullptr;
		curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);

		long response_code = 0;
		curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
		req->info->response_code = static_cast<uint16_t>(response_code);

		HTTPStatusCode status_code = HTTPUtil::ToStatusCode(req->info->response_code);
		auto resp = make_uniq<HTTPResponse>(status_code);
		resp->body = req->info->body;
		resp->url = req->info->url;
		req->response.set_value(std::move(resp));

		curl_multi_remove_handle(g->multi, easy);
		auto iter = g->ongoing_requests.find(easy);
		D_ASSERT(iter != g->ongoing_requests.end());
		g->ongoing_requests.erase(iter);
	}
}

void TimerCallback(GlobalInfo *g, int revents) {
	uint64_t count = 0;
	// Epoll leverages reactor model, need to read active bytes out.
	const int bytes_read = read(g->timer_fd, &count, sizeof(uint64_t));
	D_ASSERT(bytes_read == sizeof(uint64_t));

	curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0, &g->still_running);
	CheckMulti(g);
}

void EventCallback(GlobalInfo *g, int fd, int revents) {
	int action = ((revents & EPOLLIN) ? CURL_CSELECT_IN : 0) | ((revents & EPOLLOUT) ? CURL_CSELECT_OUT : 0);

	curl_multi_socket_action(g->multi, fd, action, &g->still_running);
	CheckMulti(g);

	if (g->still_running <= 0) {
		struct itimerspec its;
		memset(&its, 0, sizeof(its));
		const int ret = timerfd_settime(g->timer_fd, 0, &its, NULL);
		SYSCALL_THROW_IF_ERROR(ret);
	}
}

void RemoveSockInfo(SockInfo *f, GlobalInfo *g) {
	if (f == nullptr) {
		return;
	}
	if (f->sockfd != 0) {
		const int ret = epoll_ctl(g->epoll_fd, EPOLL_CTL_DEL, f->sockfd, NULL);
		SYSCALL_THROW_IF_ERROR(ret);
	}
	delete f;
}

void SetSockInfo(SockInfo *f, curl_socket_t s, CURL *e, int act, GlobalInfo *g) {
	struct epoll_event ev;
	int kind = ((act & CURL_POLL_IN) ? EPOLLIN : 0) | ((act & CURL_POLL_OUT) ? EPOLLOUT : 0);

	if (f->sockfd) {
		const int ret = epoll_ctl(g->epoll_fd, EPOLL_CTL_DEL, f->sockfd, NULL);
		SYSCALL_THROW_IF_ERROR(ret);
	}

	f->sockfd = s;
	f->action = act;
	f->easy = e;

	ev.events = kind;
	ev.data.fd = s;
	epoll_ctl(g->epoll_fd, EPOLL_CTL_ADD, s, &ev);
}

void AddSockInfo(curl_socket_t s, CURL *easy, int action, GlobalInfo *g) {
	auto *fdp = new SockInfo();
	fdp->global = g;
	SetSockInfo(fdp, s, easy, action, g);
	curl_multi_assign(g->multi, s, fdp);
}

int SocketCallback(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp) {
	auto *g = static_cast<GlobalInfo *>(cbp);
	auto *fdp = static_cast<SockInfo *>(sockp);

	if (what == CURL_POLL_REMOVE) {
		RemoveSockInfo(fdp, g);
		return 0;
	}
	if (fdp == nullptr) {
		AddSockInfo(s, e, what, g);
		return 0;
	}

	SetSockInfo(fdp, s, e, what, g);
	return 0;
}

int MultiTimerCallback(CURLM *multi, long timeout_ms, GlobalInfo *g) {
	struct itimerspec its;
	if (timeout_ms > 0) {
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = timeout_ms / 1000;
		its.it_value.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
	} else if (timeout_ms == 0) {
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 0;
		its.it_value.tv_nsec = 1;
	} else {
		memset(&its, 0, sizeof(its));
	}
	const int ret = timerfd_settime(g->timer_fd, 0, &its, NULL);
	SYSCALL_THROW_IF_ERROR(ret);
	return 0;
}

} // namespace

/*static*/ MultiCurlManager &MultiCurlManager::GetInstance() {
	static auto *manager = new MultiCurlManager();
	return *manager;
}

MultiCurlManager::MultiCurlManager() : global_info(make_uniq<GlobalInfo>()) {
	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	SYSCALL_EXIT_IF_ERROR(epoll_fd);

	int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	SYSCALL_EXIT_IF_ERROR(timer_fd);

	struct itimerspec its;
	memset(&its, 0, sizeof(its));
	its.it_value.tv_sec = 1;
	timerfd_settime(timer_fd, 0, &its, NULL);

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = timer_fd;
	int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev);
	SYSCALL_EXIT_IF_ERROR(ret);

	global_info->epoll_fd = epoll_fd;
	global_info->timer_fd = timer_fd;
	global_info->multi = curl_multi_init();

	curl_multi_setopt(global_info->multi, CURLMOPT_SOCKETFUNCTION, SocketCallback);
	curl_multi_setopt(global_info->multi, CURLMOPT_SOCKETDATA, global_info.get());
	curl_multi_setopt(global_info->multi, CURLMOPT_TIMERFUNCTION, MultiTimerCallback);
	curl_multi_setopt(global_info->multi, CURLMOPT_TIMERDATA, global_info.get());

	bkg_thread = std::thread([this]() { HandleEvent(); });
}

void MultiCurlManager::HandleEvent() {
	std::array<epoll_event, 32> events {};
	while (true) {
		// TODO(hjiang): Use wakefd for notification, instead of epoll timeout.
		const int nfds = epoll_wait(global_info->epoll_fd, events.data(), events.size(), /*timeout=*/1000);
		if (nfds < 0) {
			if (errno == EINTR) {
				continue;
			}
			SYSCALL_THROW_IF_ERROR(nfds);
		}
		for (int idx = 0; idx < nfds; ++idx) {
			if (events[idx].data.fd == global_info->timer_fd) {
				TimerCallback(global_info.get(), events[idx].events);
			} else {
				EventCallback(global_info.get(), events[idx].data.fd, events[idx].events);
			}
		}
		ProcessPendingRequests();
	}
}

void MultiCurlManager::ProcessPendingRequests() {
	while (true) {
		unique_ptr<CurlRequest> curl_request;
		{
			if (pending_requests.empty()) {
				return;
			}
			curl_request = std::move(pending_requests.front());
			pending_requests.pop();
		}

		auto *curl_request_ptr = curl_request.get();
		CURL *easy_curl = curl_request->easy_curl;
		auto iter = global_info->ongoing_requests.find(easy_curl);
		D_ASSERT(iter == global_info->ongoing_requests.end());
		global_info->ongoing_requests[easy_curl] = std::move(curl_request);

		curl_easy_setopt(easy_curl, CURLOPT_PRIVATE, curl_request_ptr);
		curl_multi_add_handle(global_info->multi, easy_curl);
	}
}

unique_ptr<HTTPResponse> MultiCurlManager::HandleRequest(unique_ptr<CurlRequest> request) {
	auto resp_fut = request->response.get_future();
	{
		std::lock_guard<std::mutex> lck(mu);
		pending_requests.emplace(std::move(request));
	}
	return resp_fut.get();
}

} // namespace duckdb
