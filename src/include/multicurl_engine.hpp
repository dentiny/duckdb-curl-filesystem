#pragma once

#include <mutex>
#include <curl/curl.h>
#include <future>

#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/map.hpp"
#include "duckdb/common/http_util.hpp"
#include "httpfs_client.hpp"
#include "http_state.hpp"
#include "curl_request.hpp"

namespace duckdb {

class MultiCurlEngine {
public:
	static MultiCurlEngine &Instance() {
		static MultiCurlEngine inst;
		return inst;
	}

	unique_ptr<HTTPResponse> Perform(EasyRequest *req);

	static void GlobalInit();
	static void GlobalCleanup();

	int OnSocket(curl_socket_t s, int what, EasyRequest *req);
	int OnTimer();

	CURLM *MultiHandle() {
		return multi_;
	}
	int EpollFd() const {
		return epoll_fd_;
	}
	int TimerFd() const {
		return timer_fd_;
	}
	int WakeFd() const {
		return wake_fd_;
	}

private:
	MultiCurlEngine();
	~MultiCurlEngine();

	void Start();
	void Stop();
	void WorkerLoop();
	void ArmTimer(long ms);

	static int CurlSocketCallback(CURL *easy, curl_socket_t s, int what, void *userp, void *socketp);
	static int CurlTimerCallback(CURLM *multi, long timeout_ms, void *userp);

private:
	CURLM *multi_ = nullptr;
	int epoll_fd_ = -1;
	int timer_fd_ = -1;
	int wake_fd_ = -1;

	std::atomic<bool> running_ {false};
	std::thread worker_;

	// socket -> bitmask currently registered in epoll
	std::mutex mtx_;
	std::map<curl_socket_t, uint32_t> sock_interest_;

	// global refs
	inline static std::mutex global_mtx_;
	inline static std::atomic<int> global_refs_ {0};
};

} // namespace duckdb
