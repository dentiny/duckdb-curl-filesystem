#include "curl_request.hpp"

#include <iostream>

namespace duckdb {

EasyRequest::EasyRequest(string url) : info(make_uniq<RequestInfo>()) {
	info->url = std::move(url);
	easy = curl_easy_init();
	if (easy == nullptr) {
		throw InternalException("Failed to initialize curl easy");
	}

	curl_easy_setopt(easy, CURLOPT_URL, info->url.c_str());
	curl_easy_setopt(easy, CURLOPT_PROGRESSFUNCTION, EasyRequest::ProgressCallback);
	curl_easy_setopt(easy, CURLOPT_PROGRESSDATA, this);

	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, EasyRequest::WriteBody);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);

	curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, EasyRequest::WriteHeader);
	curl_easy_setopt(easy, CURLOPT_HEADERDATA, this);

	curl_easy_setopt(easy, CURLOPT_PRIVATE, this);

	// For debugging purpose.
	curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
}

EasyRequest::~EasyRequest() {
	if (headers != nullptr) {
		curl_slist_free_all(headers);
	}
	if (easy != nullptr) {
		curl_easy_cleanup(easy);
	}
}

/*static*/ size_t EasyRequest::WriteBody(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	auto *req = static_cast<EasyRequest *>(userp);
	req->info->body.append(static_cast<char *>(contents), total_size);
	if (req->get_info && req->get_info->content_handler) {
		req->get_info->content_handler(
		    const_data_ptr_cast(req->info->body.data() + req->info->body.size() - total_size), total_size);
	}
	return total_size;
}

/*static*/ size_t EasyRequest::WriteHeader(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	auto *req = static_cast<EasyRequest *>(userp);
	std::string header(static_cast<char *>(contents), total_size);
	// Trim trailing \r\n
	if (!header.empty() && header.back() == '\n') {
		header.pop_back();
		if (!header.empty() && header.back() == '\r') {
			header.pop_back();
		}
	}
	// Redirect restart
	if (header.rfind("HTTP/", 0) == 0) {
		req->info->header_collection.push_back(HTTPHeaders());
		req->info->header_collection.back().Insert("__RESPONSE_STATUS__", header);
	}
	size_t colon_pos = header.find(':');
	if (colon_pos != std::string::npos) {
		std::string k = header.substr(0, colon_pos);
		std::string v = header.substr(colon_pos + 1);
		if (!v.empty() && v[0] == ' ')
			v.erase(0, 1);
		if (req->info->header_collection.empty()) {
			req->info->header_collection.push_back(HTTPHeaders());
		}
		req->info->header_collection.back().Insert(k, v);
	}
	return total_size;
}

/*static*/ int EasyRequest::ProgressCallback(void *p, double dltotal, double dlnow, double ult, double uln) {
	std::cerr << "Progress: " << dlnow << dltotal << std::endl;
	;
}

} // namespace duckdb
