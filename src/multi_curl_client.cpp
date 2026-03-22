#include "multi_curl_client.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <atomic>
#include <curl/curl.h>
#include <sys/stat.h>

#include "duckdb/common/exception/http_exception.hpp"
#include "multi_curl_manager.hpp"

namespace duckdb {

namespace {

std::atomic<idx_t> multi_curl_client_count {0};

static std::string certFileLocations[] = {
    "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
    "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem"};

static std::string SelectCURLCertPath() {
	for (std::string &caFile : certFileLocations) {
		struct stat buf;
		if (stat(caFile.c_str(), &buf) == 0) {
			return caFile;
		}
	}
	return std::string();
}

static size_t RequestWriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	std::string *str = static_cast<std::string *>(userp);
	str->append(static_cast<char *>(contents), total_size);
	return total_size;
}

static size_t RequestHeaderCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	std::string header(static_cast<char *>(contents), total_size);
	HeaderCollector *header_collection = static_cast<HeaderCollector *>(userp);

	if (!header.empty() && header.back() == '\n') {
		header.pop_back();
		if (!header.empty() && header.back() == '\r') {
			header.pop_back();
		}
	}

	if (header.rfind("HTTP/", 0) == 0) {
		header_collection->header_collection.push_back(HTTPHeaders());
		header_collection->header_collection.back().Insert("__RESPONSE_STATUS__", header);
	}

	size_t colonPos = header.find(':');

	if (colonPos != std::string::npos) {
		std::string part1 = header.substr(0, colonPos);
		std::string part2 = header.substr(colonPos + 1);
		if (part2.at(0) == ' ') {
			part2.erase(0, 1);
		}
		header_collection->header_collection.back().Insert(part1, part2);
	}

	return total_size;
}

} // namespace

MultiCurlClient::MultiCurlClient(HTTPFSParams &http_params, const string &proto_host_port) {
	Initialize(http_params);
}

void MultiCurlClient::Initialize(HTTPParams &http_p) {
	HTTPFSParams &http_params = reinterpret_cast<HTTPFSParams &>(http_p);
	auto bearer_token = "";
	if (!http_params.bearer_token.empty()) {
		bearer_token = http_params.bearer_token.c_str();
	}
	state = http_params.state;

	InitCurlGlobal();

	std::string cert_file_path;
	if (!http_params.ca_cert_file.empty()) {
		cert_file_path = http_params.ca_cert_file;
	} else {
		cert_file_path = SelectCURLCertPath();
	}
	curl = make_uniq<CURLHandle>(bearer_token, cert_file_path);
	request_info = make_uniq<RequestInfo>();

	curl_easy_setopt(*curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
	curl_easy_setopt(*curl, CURLOPT_FOLLOWLOCATION, 1L);

	if (!http_params.keep_alive) {
		curl_easy_setopt(*curl, CURLOPT_FORBID_REUSE, 1L);
	}

	if (http_params.enable_curl_server_cert_verification) {
		curl_easy_setopt(*curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(*curl, CURLOPT_SSL_VERIFYHOST, 2L);
	} else {
		curl_easy_setopt(*curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(*curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	curl_easy_setopt(*curl, CURLOPT_TIMEOUT, http_params.timeout);
	curl_easy_setopt(*curl, CURLOPT_CONNECTTIMEOUT, http_params.timeout);
	curl_easy_setopt(*curl, CURLOPT_ACCEPT_ENCODING, "identity");
	curl_easy_setopt(*curl, CURLOPT_FOLLOWLOCATION, 1L);

	curl_easy_setopt(*curl, CURLOPT_HEADERFUNCTION, RequestHeaderCallback);
	curl_easy_setopt(*curl, CURLOPT_HEADERDATA, &request_info->header_collection);
	curl_easy_setopt(*curl, CURLOPT_WRITEFUNCTION, RequestWriteCallback);
	curl_easy_setopt(*curl, CURLOPT_WRITEDATA, &request_info->body);

	if (!http_params.http_proxy.empty()) {
		curl_easy_setopt(*curl, CURLOPT_PROXY,
		                 StringUtil::Format("%s:%s", http_params.http_proxy, http_params.http_proxy_port).c_str());

		if (!http_params.http_proxy_username.empty()) {
			curl_easy_setopt(*curl, CURLOPT_PROXYUSERNAME, http_params.http_proxy_username.c_str());
			curl_easy_setopt(*curl, CURLOPT_PROXYPASSWORD, http_params.http_proxy_password.c_str());
		}
	}
}

MultiCurlClient::~MultiCurlClient() {
	DestroyCurlGlobal();
}

unique_ptr<HTTPResponse> MultiCurlClient::Get(GetRequestInfo &info) {
	if (state) {
		state->get_count++;
	}

	auto curl_headers = TransformHeadersCurl(info.headers, info.params);
	string url = info.url;

	auto req = make_uniq<CurlRequest>(*curl);
	req->SetUrl(std::move(url));
	req->SetHeaders(curl_headers.headers);
	req->SetGetAttrs();

	auto response = MultiCurlManager::GetInstance().HandleRequest(std::move(req));
	if (state) {
		state->total_bytes_received += response->body.size();
	}
	if (info.response_handler) {
		if (!info.response_handler(*response)) {
			return response;
		}
	}
	if (info.content_handler) {
		info.content_handler(const_data_ptr_cast(response->body.c_str()), response->body.size());
	}
	return response;
}

unique_ptr<HTTPResponse> MultiCurlClient::Put(PutRequestInfo &info) {
	if (state) {
		state->put_count++;
		state->total_bytes_sent += info.buffer_in_len;
	}

	auto curl_headers = TransformHeadersCurl(info.headers, info.params);
	curl_headers.Add("Content-Type: " + info.content_type);
	request_info->url = info.url;

	CURLcode res;
	{
		curl_easy_setopt(*curl, CURLOPT_URL, request_info->url.c_str());
		curl_easy_setopt(*curl, CURLOPT_CUSTOMREQUEST, "PUT");
		curl_easy_setopt(*curl, CURLOPT_POSTFIELDS, const_char_ptr_cast(info.buffer_in));
		curl_easy_setopt(*curl, CURLOPT_POSTFIELDSIZE, info.buffer_in_len);
		curl_easy_setopt(*curl, CURLOPT_HTTPHEADER, curl_headers ? curl_headers.headers : nullptr);
		res = curl->Execute();
	}

	curl_easy_getinfo(*curl, CURLINFO_RESPONSE_CODE, &request_info->response_code);
	return TransformResponseCurl(res);
}

unique_ptr<HTTPResponse> MultiCurlClient::Head(HeadRequestInfo &info) {
	if (state) {
		state->head_count++;
	}

	auto curl_headers = TransformHeadersCurl(info.headers, info.params);
	string url = info.url;

	auto req = make_uniq<CurlRequest>(*curl);
	req->SetUrl(std::move(url));
	req->SetHeaders(curl_headers.headers);
	req->SetHeadAttrs();

	auto response = MultiCurlManager::GetInstance().HandleRequest(std::move(req));
	return response;
}

unique_ptr<HTTPResponse> MultiCurlClient::Delete(DeleteRequestInfo &info) {
	if (state) {
		state->delete_count++;
	}

	auto curl_headers = TransformHeadersCurl(info.headers, info.params);
	request_info->url = info.url;

	CURLcode res;
	{
		curl_easy_setopt(*curl, CURLOPT_URL, request_info->url.c_str());
		curl_easy_setopt(*curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		curl_easy_setopt(*curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(*curl, CURLOPT_HTTPHEADER, curl_headers ? curl_headers.headers : nullptr);
		res = curl->Execute();
	}

	curl_easy_getinfo(*curl, CURLINFO_RESPONSE_CODE, &request_info->response_code);
	return TransformResponseCurl(res);
}

unique_ptr<HTTPResponse> MultiCurlClient::Post(PostRequestInfo &info) {
	if (state) {
		state->post_count++;
		state->total_bytes_sent += info.buffer_in_len;
	}

	auto curl_headers = TransformHeadersCurl(info.headers, info.params);
	const string content_type = "Content-Type: application/octet-stream";
	curl_headers.Add(content_type.c_str());
	request_info->url = info.url;

	CURLcode res;
	{
		curl_easy_setopt(*curl, CURLOPT_URL, request_info->url.c_str());
		curl_easy_setopt(*curl, CURLOPT_POST, 1L);
		curl_easy_setopt(*curl, CURLOPT_POSTFIELDS, const_char_ptr_cast(info.buffer_in));
		curl_easy_setopt(*curl, CURLOPT_POSTFIELDSIZE, info.buffer_in_len);
		curl_easy_setopt(*curl, CURLOPT_HTTPHEADER, curl_headers ? curl_headers.headers : nullptr);
		res = curl->Execute();
	}

	curl_easy_getinfo(*curl, CURLINFO_RESPONSE_CODE, &request_info->response_code);
	info.buffer_out = request_info->body;
	return TransformResponseCurl(res);
}

CURLRequestHeaders MultiCurlClient::TransformHeadersCurl(const HTTPHeaders &header_map, const HTTPParams &params) {
	auto &httpfs_params = params.Cast<HTTPFSParams>();

	std::vector<std::string> headers;
	for (auto &entry : header_map) {
		const std::string new_header = entry.first + ": " + entry.second;
		headers.push_back(new_header);
	}
	CURLRequestHeaders curl_headers;
	for (auto &header : headers) {
		curl_headers.Add(header);
	}
	if (!httpfs_params.pre_merged_headers) {
		for (auto &entry : params.extra_headers) {
			curl_headers.Add(entry.first + ": " + entry.second);
		}
	}
	return curl_headers;
}

void MultiCurlClient::ResetRequestInfo() {
	request_info->header_collection.clear();
	request_info->body = "";
	request_info->url = "";
	request_info->response_code = 0;
}

unique_ptr<HTTPResponse> MultiCurlClient::TransformResponseCurl(CURLcode res) {
	auto status_code = HTTPStatusCode(request_info->response_code);
	auto response = make_uniq<HTTPResponse>(status_code);
	if (res != CURLcode::CURLE_OK) {
		if (!request_info->header_collection.empty() &&
		    request_info->header_collection.back().HasHeader("__RESPONSE_STATUS__")) {
			response->request_error = request_info->header_collection.back().GetHeaderValue("__RESPONSE_STATUS__");
		} else {
			response->request_error = curl_easy_strerror(res);
		}
		return response;
	}
	response->body = request_info->body;
	response->url = request_info->url;
	if (!request_info->header_collection.empty()) {
		for (auto &header : request_info->header_collection.back()) {
			response->headers.Insert(header.first, header.second);
		}
	}
	ResetRequestInfo();
	return response;
}

void MultiCurlClient::InitCurlGlobal() {
	if (multi_curl_client_count == 0) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
	++multi_curl_client_count;
}

void MultiCurlClient::DestroyCurlGlobal() {
}

} // namespace duckdb
