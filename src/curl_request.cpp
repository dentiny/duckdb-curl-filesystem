#include "curl_request.hpp"

#include "duckdb/common/assert.hpp"

namespace duckdb {

CurlRequest::CurlRequest(std::string url) : info(make_uniq<RequestInfo>()) {
	info->url = std::move(url);
	easy_curl = curl_easy_init();
	D_ASSERT(easy != nullptr);

	curl_easy_setopt(easy_curl, CURLOPT_URL, info->url.c_str());
	curl_easy_setopt(easy_curl, CURLOPT_HEADERFUNCTION, CurlRequest::WriteHeader);
	curl_easy_setopt(easy_curl, CURLOPT_HEADERDATA, this);
	curl_easy_setopt(easy_curl, CURLOPT_WRITEFUNCTION, CurlRequest::WriteBody);
	curl_easy_setopt(easy_curl, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(easy_curl, CURLOPT_PRIVATE, this);
	// TODO(hjiang): Enable verbose logging when enabled.
	// curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
}

CurlRequest::~CurlRequest() {
	D_ASSERT(easy_curl != nullptr);
	curl_easy_cleanup(easy_curl);
}

/*static*/ size_t CurlRequest::WriteHeader(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	std::string header(static_cast<char *>(contents), total_size);
	auto *req = static_cast<CurlRequest *>(userp);
	auto &header_collection = req->info->header_collection;

	// Trim trailing \r\n
	if (!header.empty() && header.back() == '\n') {
		header.pop_back();
		if (!header.empty() && header.back() == '\r') {
			header.pop_back();
		}
	}

	// If header starts with HTTP/... curl has followed a redirect and we have a new Header,
	// so we push back a new header_collection and store headers from the redirect there.
	if (header.rfind("HTTP/", 0) == 0) {
		header_collection.push_back(HTTPHeaders());
		header_collection.back().Insert("__RESPONSE_STATUS__", header);
	}

	size_t colon_pos = header.find(':');

	if (colon_pos != std::string::npos) {
		// Split the string into two parts
		std::string part1 = header.substr(0, colon_pos);
		std::string part2 = header.substr(colon_pos + 1);
		if (part2.at(0) == ' ') {
			part2.erase(0, 1);
		}

		header_collection.back().Insert(part1, part2);
	}
	return total_size;
}

/*static*/ size_t CurlRequest::WriteBody(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	auto *req = static_cast<CurlRequest *>(userp);
	req->info->body.append(static_cast<char *>(contents), total_size);
	return total_size;
}

} // namespace duckdb
