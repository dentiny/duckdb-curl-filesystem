#pragma once

#include <atomic>
#include <future>

#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/map.hpp"
#include "duckdb/common/http_util.hpp"
#include "httpfs_client.hpp"
#include "http_state.hpp"

namespace duckdb {

struct RequestInfo {
	std::string url;
	std::string body;
	uint16_t response_code = 0;
};

struct CurlRequest {
	unique_ptr<RequestInfo> info;
	std::promise<unique_ptr<HTTPResponse>> done;
	std::atomic<bool> completed {false};
	CURL *easy_curl = nullptr;

	explicit CurlRequest(std::string url);
	~CurlRequest();

	static size_t WriteBody(void *contents, size_t size, size_t nmemb, void *userp);
};

} // namespace duckdb
