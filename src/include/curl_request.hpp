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

struct EasyRequest {
	unique_ptr<RequestInfo> info;
	std::promise<unique_ptr<HTTPResponse>> done;
	std::atomic<bool> completed {false};
	CURL *easy = nullptr;
	curl_slist *headers = nullptr;
	optional_ptr<HTTPState> state = nullptr;
	GetRequestInfo *get_info = nullptr;

	EasyRequest() : info(make_uniq<RequestInfo>()) {
		easy = curl_easy_init();
		if (easy == nullptr) {
			throw InternalException("Failed to initialize curl easy");
		}

        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, EasyRequest::WriteBody);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);

        curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, EasyRequest::WriteHeader);
        curl_easy_setopt(easy, CURLOPT_HEADERDATA, this);

        curl_easy_setopt(easy, CURLOPT_PRIVATE, this);
	}
	~EasyRequest() {
		if (headers) {
			curl_slist_free_all(headers);
		}
		if (easy) {
			curl_easy_cleanup(easy);
		}
	}

	static size_t WriteBody(void *contents, size_t size, size_t nmemb, void *userp);
	static size_t WriteHeader(void *contents, size_t size, size_t nmemb, void *userp);
};

} // namespace duckdb
