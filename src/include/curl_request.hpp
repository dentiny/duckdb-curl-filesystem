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
	string url = "";
	string body = "";
	uint16_t response_code = 0;
	std::vector<HTTPHeaders> header_collection;
};

struct CurlRequest {
	unique_ptr<RequestInfo> info;
	std::promise<unique_ptr<HTTPResponse>> done;
	std::atomic<bool> completed {false};
	CURL *easy = nullptr;
	curl_slist *headers = nullptr;
	optional_ptr<HTTPState> state = nullptr;
	GetRequestInfo *get_info = nullptr;

	CurlRequest(string url);
	~CurlRequest();

	static int ProgressCallback(void *p, double dltotal, double dlnow, double ult, double uln);
	static size_t WriteBody(void *contents, size_t size, size_t nmemb, void *userp);
};

} // namespace duckdb
