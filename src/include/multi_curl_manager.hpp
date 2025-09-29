// MultiCurlManager is a class which wraps around events managed by multi-curl, and use epoll as polling engine.
#pragma once

#include <curl/curl.h>

#include <thread>

#include "curl_request.hpp"
#include "duckdb/common/unique_ptr.hpp"

namespace duckdb {

struct GlobalInfo {
	int epoll_fd;
	int timer_fd;
	CURLM *multi = nullptr;
	// Used to protect concurrent access against multi-curl, which is not thread-safe.
	std::mutex mu;
	int still_running = 0;
};

class MultiCurlManager {
public:
	static MultiCurlManager &GetInstance();
	// Never destructs.
	~MultiCurlManager() = default;

	// Disable copy / move constructor / assignment.
	MultiCurlManager(const MultiCurlManager &) = delete;
	MultiCurlManager &operator=(const MultiCurlManager &) = delete;

	// Handle HTTP request, block wait until its completion.
	unique_ptr<HTTPResponse> HandleRequest(unique_ptr<EasyRequest> request);

private:
	MultiCurlManager();

	// Poll and handle epoll events.
	void HandleEvent();

	GlobalInfo global_info;
	// Background thread to handle all events.
	std::thread bkg_thread;
};

} // namespace duckdb
