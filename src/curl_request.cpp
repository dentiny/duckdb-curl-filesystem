#include "curl_request.hpp"

namespace duckdb {

CurlRequest::CurlRequest(std::string url) : info(make_uniq<RequestInfo>()) {
	info->url = std::move(url);
	easy = curl_easy_init();
	D_ASSERT(easy != nullptr);

	curl_easy_setopt(easy, CURLOPT_URL, info->url.c_str());
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, CurlRequest::WriteBody);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(easy, CURLOPT_PRIVATE, this);
	// TODO(hjiang): Enable verbose logging when enabled.
	// curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
}

CurlRequest::~CurlRequest() {
	D_ASSERT(easy != nullptr);
	curl_easy_cleanup(easy);
}

/*static*/ size_t CurlRequest::WriteBody(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	auto *req = static_cast<CurlRequest *>(userp);
	req->info->body.append(static_cast<char *>(contents), total_size);
	return total_size;
}

} // namespace duckdb
