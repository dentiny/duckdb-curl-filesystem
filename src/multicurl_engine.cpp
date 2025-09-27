#include "multicurl_engine.hpp"

#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define CHECK_CURL_OK(expr)  D_ASSERT((expr) == CURLE_OK)
#define CHECK_CURLM_OK(expr) D_ASSERT((expr) == CURLM_OK)

namespace duckdb {

// Global init/cleanup refcounted
/*static*/ void MultiCurlEngine::GlobalInit() {
	std::lock_guard<std::mutex> lg(global_mtx_);
	if (global_refs_++ == 0) {
		CHECK_CURL_OK(curl_global_init(CURL_GLOBAL_DEFAULT));
		Instance();
	}
}
/*static*/ void MultiCurlEngine::GlobalCleanup() {
	std::lock_guard<std::mutex> lg(global_mtx_);
	if (--global_refs_ == 0) {
		Instance().Stop();
		curl_global_cleanup();
	}
}

MultiCurlEngine::MultiCurlEngine() {
	multi_ = curl_multi_init();
	if (!multi_)
		throw InternalException("Failed to init curl multi");

	epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd_ < 0)
		throw InternalException("epoll_create1 failed");

	timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (timer_fd_ < 0)
		throw InternalException("timerfd_create failed");

	wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (wake_fd_ < 0)
		throw InternalException("eventfd failed");

	// Attach callbacks
	CHECK_CURLM_OK(curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, &MultiCurlEngine::CurlSocketCallback));
	CHECK_CURLM_OK(curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this));
	CHECK_CURLM_OK(curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, &MultiCurlEngine::CurlTimerCallback));
	CHECK_CURLM_OK(curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this));

	// Register timerfd and wakefd with epoll
	epoll_event ev {};
	ev.events = EPOLLIN;
	ev.data.fd = timer_fd_;
	if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &ev) < 0)
		throw InternalException("epoll_ctl timer_fd failed");

	ev = {};
	ev.events = EPOLLIN;
	ev.data.fd = wake_fd_;
	if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_fd_, &ev) < 0)
		throw InternalException("epoll_ctl wake_fd failed");

	Start();
}

MultiCurlEngine::~MultiCurlEngine() {
	Stop();
	if (multi_)
		curl_multi_cleanup(multi_);
	if (epoll_fd_ >= 0)
		close(epoll_fd_);
	if (timer_fd_ >= 0)
		close(timer_fd_);
	if (wake_fd_ >= 0)
		close(wake_fd_);
}

void MultiCurlEngine::Start() {
	bool expected = false;
	if (running_.compare_exchange_strong(expected, true)) {
		worker_ = std::thread([this] { WorkerLoop(); });
	}
}

void MultiCurlEngine::Stop() {
	bool expected = true;
	if (running_.compare_exchange_strong(expected, false)) {
		// poke worker
		uint64_t one = 1;
		(void)write(wake_fd_, &one, sizeof(one));
		if (worker_.joinable())
			worker_.join();
	}
}

void MultiCurlEngine::WorkerLoop() {
	constexpr int MAX_EVENTS = 32;
	std::array<epoll_event, MAX_EVENTS> events;
	while (running_) {
		int n = epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, -1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (int i = 0; i < n; ++i) {
			int fd = events[i].data.fd;
			uint32_t ev = events[i].events;

			if (fd == timer_fd_) {
				uint64_t expirations;
				(void)read(timer_fd_, &expirations, sizeof(expirations)); // drain
				OnTimer();
				continue;
			}
			if (fd == wake_fd_) {
				uint64_t drained;
				(void)read(wake_fd_, &drained, sizeof(drained)); // drain
				continue;
			}

			int action = 0;
			if (ev & (EPOLLIN | EPOLLHUP | EPOLLERR))
				action |= CURL_CSELECT_IN;
			if (ev & (EPOLLOUT | EPOLLHUP | EPOLLERR))
				action |= CURL_CSELECT_OUT;

			int running_handles = 0;
			CHECK_CURLM_OK(curl_multi_socket_action(multi_, fd, action, &running_handles));

			// Read out completed transfers
			int msgs_in_queue = 0;
			while (CURLMsg *msg = curl_multi_info_read(multi_, &msgs_in_queue)) {
				if (msg->msg == CURLMSG_DONE) {
					CURL *easy = msg->easy_handle;
					EasyRequest *req = nullptr;
					curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);
					long response_code = 0;
					curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
					req->ri->response_code = static_cast<uint16_t>(response_code);

					// Build response
					auto status_code = HTTPStatusCode(req->ri->response_code);
					auto resp = make_uniq<HTTPResponse>(status_code);

					if (msg->data.result != CURLE_OK) {
						if (!req->ri->header_collection.empty() &&
						    req->ri->header_collection.back().HasHeader("__RESPONSE_STATUS__")) {
							resp->request_error =
							    req->ri->header_collection.back().GetHeaderValue("__RESPONSE_STATUS__");
						} else {
							resp->request_error = curl_easy_strerror(msg->data.result);
						}
					} else {
						resp->body = req->ri->body;
						resp->url = req->ri->url;
						if (!req->ri->header_collection.empty()) {
							for (auto &h : req->ri->header_collection.back()) {
								resp->headers.Insert(h.first, h.second);
							}
						}
					}

					// accounting
					if (req->state) {
						if (!req->ri->header_collection.empty() &&
						    req->ri->header_collection.back().HasHeader("content-length")) {
							auto br = std::stoi(req->ri->header_collection.back().GetHeaderValue("content-length"));
							req->state->total_bytes_received += idx_t(br);
						} else {
							req->state->total_bytes_received += idx_t(req->ri->body.size());
						}
					}

					curl_multi_remove_handle(multi_, easy);
					req->completed = true;
					req->done.set_value(std::move(resp));
				}
			}
		}
	}
}

int MultiCurlEngine::OnSocket(curl_socket_t s, int what, EasyRequest * /*req*/) {
	std::lock_guard<std::mutex> lg(mtx_);
	uint32_t ev = 0;
	switch (what) {
	case CURL_POLL_IN:
		ev = EPOLLIN;
		break;
	case CURL_POLL_OUT:
		ev = EPOLLOUT;
		break;
	case CURL_POLL_INOUT:
		ev = EPOLLIN | EPOLLOUT;
		break;
	case CURL_POLL_REMOVE: {
		if (sock_interest_.count(s)) {
			epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, s, nullptr);
			sock_interest_.erase(s);
		}
		return 0;
	}
	default:
		break;
	}

	epoll_event e {};
	e.data.fd = s;
	e.events = ev | EPOLLET; // edge-triggered
	if (!sock_interest_.count(s)) {
		if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, s, &e) == 0) {
			sock_interest_[s] = e.events;
		}
	} else {
		if (sock_interest_[s] != e.events) {
			(void)epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, s, &e);
			sock_interest_[s] = e.events;
		}
	}
	return 0;
}

void MultiCurlEngine::ArmTimer(long ms) {
	itimerspec its {};
	if (ms < 0) {
		// disarm
		its.it_value.tv_sec = 0;
		its.it_value.tv_nsec = 0;
	} else {
		its.it_value.tv_sec = ms / 1000;
		its.it_value.tv_nsec = (ms % 1000) * 1000000;
	}
	(void)timerfd_settime(timer_fd_, 0, &its, nullptr);
}

int MultiCurlEngine::OnTimer() {
	int running_handles = 0;
	CHECK_CURLM_OK(curl_multi_socket_action(multi_, CURL_SOCKET_TIMEOUT, 0, &running_handles));

	int msgs_in_queue = 0;
	while (CURLMsg *msg = curl_multi_info_read(multi_, &msgs_in_queue)) {
		if (msg->msg == CURLMSG_DONE) {
			CURL *easy = msg->easy_handle;
			EasyRequest *req = nullptr;
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);
			long response_code = 0;
			curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
			req->ri->response_code = static_cast<uint16_t>(response_code);

			auto status_code = HTTPStatusCode(req->ri->response_code);
			auto resp = make_uniq<HTTPResponse>(status_code);
			if (msg->data.result != CURLE_OK) {
				if (!req->ri->header_collection.empty() &&
				    req->ri->header_collection.back().HasHeader("__RESPONSE_STATUS__")) {
					resp->request_error = req->ri->header_collection.back().GetHeaderValue("__RESPONSE_STATUS__");
				} else {
					resp->request_error = curl_easy_strerror(msg->data.result);
				}
			} else {
				resp->body = req->ri->body;
				resp->url = req->ri->url;
				if (!req->ri->header_collection.empty()) {
					for (auto &h : req->ri->header_collection.back()) {
						resp->headers.Insert(h.first, h.second);
					}
				}
			}

			curl_multi_remove_handle(multi_, easy);
			req->completed = true;
			req->done.set_value(std::move(resp));
		}
	}
	return 0;
}

int MultiCurlEngine::CurlSocketCallback(CURL *easy, curl_socket_t s, int what, void *userp, void *socketp) {
	auto *engine = static_cast<MultiCurlEngine *>(userp);
	auto *req = static_cast<EasyRequest *>(socketp);
	(void)easy;
	return engine->OnSocket(s, what, req);
}

int MultiCurlEngine::CurlTimerCallback(CURLM * /*multi*/, long timeout_ms, void *userp) {
	auto *engine = static_cast<MultiCurlEngine *>(userp);
	engine->ArmTimer(timeout_ms);
	return 0;
}

unique_ptr<HTTPResponse> MultiCurlEngine::Perform(EasyRequest *req) {
	// Associate private data and add to multi
	CHECK_CURL_OK(curl_easy_setopt(req->easy, CURLOPT_PRIVATE, req));
	CHECK_CURLM_OK(curl_multi_add_handle(multi_, req->easy));

	// Kick worker via wakefd so epoll loop returns
	uint64_t one = 1;
	(void)write(wake_fd_, &one, sizeof(one));

	// Wait
	auto fut = req->done.get_future();
	auto resp = fut.get(); // synchronous API surface
	return resp;
}

} // namespace duckdb
