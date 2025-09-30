#pragma once

#include <cstdint>
#include <future>
#include <vector>

#include "duckdb/common/http_util.hpp"
#include "duckdb/common/map.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "httpfs_client.hpp"
#include "http_state.hpp"

namespace duckdb {

struct RequestInfo {
	string url = "";
	string body = "";
	uint16_t response_code = 0;
	std::vector<HTTPHeaders> header_collection;
};

struct CurlRequest {
	unique_ptr<RequestInfo> info;
	std::promise<unique_ptr<HTTPResponse>> done;
	CURL *easy_curl = nullptr;

	explicit CurlRequest(std::string url);
	~CurlRequest();

	static size_t WriteHeader(void *contents, size_t size, size_t nmemb, void *userp);
	static size_t WriteBody(void *contents, size_t size, size_t nmemb, void *userp);
};

} // namespace duckdb
