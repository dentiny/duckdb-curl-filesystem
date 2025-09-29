#include "curl_request.hpp"

namespace duckdb {

/*static*/ size_t EasyRequest::WriteBody(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	auto *req = static_cast<EasyRequest *>(userp);
	req->info->body.append(static_cast<char *>(contents), total_size);
	if (req->get_info && req->get_info->content_handler) {
		req->get_info->content_handler(const_data_ptr_cast(req->info->body.data() + req->info->body.size() - total_size),
		                               total_size);
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

} // namespace duckdb
