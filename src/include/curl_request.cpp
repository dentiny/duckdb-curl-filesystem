#include "curl_request.hpp"

namespace duckdb {

/*static*/ size_t EasyRequest::WriteBody(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	auto *req = static_cast<EasyRequest *>(userp);
	req->ri->body.append(static_cast<char *>(contents), total_size);
	if (req->get_info && req->get_info->content_handler) {
		req->get_info->content_handler(const_data_ptr_cast(req->ri->body.data() + req->ri->body.size() - total_size),
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
		req->ri->header_collection.push_back(HTTPHeaders());
		req->ri->header_collection.back().Insert("__RESPONSE_STATUS__", header);
	}
	size_t colonPos = header.find(':');
	if (colonPos != std::string::npos) {
		std::string k = header.substr(0, colonPos);
		std::string v = header.substr(colonPos + 1);
		if (!v.empty() && v[0] == ' ')
			v.erase(0, 1);
		if (req->ri->header_collection.empty()) {
			req->ri->header_collection.push_back(HTTPHeaders());
		}
		req->ri->header_collection.back().Insert(k, v);
	}
	return total_size;
}

} // namespace duckdb
