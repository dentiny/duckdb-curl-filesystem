#pragma once

#include <curl/curl.h>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

#include "duckdb/common/unique_ptr.hpp"
#include "curl_request.hpp"
#include "duckdb/common/http_util.hpp"
#include "duckdb/common/helper.hpp"

namespace duckdb {

struct GlobalInfo {
	int epoll_fd = -1;
	int timer_fd = -1;
	CURLM *multi = nullptr;
	std::mutex mu;
	int still_running = 0;
};

class MultiCurlManager {
public:
	static MultiCurlManager &GetInstance();
	~MultiCurlManager() = default;

	MultiCurlManager(const MultiCurlManager &) = delete;
	MultiCurlManager &operator=(const MultiCurlManager &) = delete;

	unique_ptr<HTTPResponse> HandleRequest(unique_ptr<CurlRequest> request);

private:
	MultiCurlManager();

	void HandleEvent();
	void ProcessPendingRequests();

	unique_ptr<GlobalInfo> global_info;
	std::queue<unique_ptr<CurlRequest>> pending_requests_;
	std::unordered_map<CURL *, unique_ptr<CurlRequest>> ongoing_requests_;
	std::thread bkg_thread;
};

} // namespace duckdb
