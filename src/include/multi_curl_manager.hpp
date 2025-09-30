#pragma once

#include <curl/curl.h>
#include <future>
#include <mutex>
#include <thread>

#include "curl_request.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/http_util.hpp"
#include "duckdb/common/queue.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/unordered_map.hpp"

namespace duckdb {

struct GlobalInfo {
	int epoll_fd = -1;
	int timer_fd = -1;
	CURLM *multi = nullptr;
	int still_running = 0;
	// Only accessed in the background thread.
	unordered_map<CURL *, unique_ptr<CurlRequest>> ongoing_requests;
};

class MultiCurlManager {
public:
	static MultiCurlManager &GetInstance();
	~MultiCurlManager() = default;

	// Disable copy / move constructor / assignment.
	MultiCurlManager(const MultiCurlManager &) = delete;
	MultiCurlManager &operator=(const MultiCurlManager &) = delete;

	// Handle the given request, and block wait until its completion.
	unique_ptr<HTTPResponse> HandleRequest(unique_ptr<CurlRequest> request);

private:
	MultiCurlManager();

	// Epoll-based eventloop.
	void HandleEvent();
	// Process all pending requests and bind easy curl handle with multi curl handle.
	void ProcessPendingRequests();

	unique_ptr<GlobalInfo> global_info;
	// Used to protect [`pending_requests`].
	std::mutex mu;
	queue<unique_ptr<CurlRequest>> pending_requests;
	// Background thread which keeps polling with polling engine.
	std::thread bkg_thread;
};

} // namespace duckdb
