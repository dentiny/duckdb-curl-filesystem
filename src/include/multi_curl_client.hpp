#pragma once

#include "duckdb/common/http_util.hpp"
#include "httpfs_client.hpp"
#include "httpfs_curl_client.hpp"
#include "http_state.hpp"
#include "curl_request.hpp"

namespace duckdb {

class MultiCurlClient : public HTTPClient {
public:
	MultiCurlClient(HTTPFSParams &http_params, const string &proto_host_port);
	~MultiCurlClient();

	void Initialize(HTTPParams &http_params) override;

	unique_ptr<HTTPResponse> Get(GetRequestInfo &info) override;
	unique_ptr<HTTPResponse> Put(PutRequestInfo &info) override;
	unique_ptr<HTTPResponse> Head(HeadRequestInfo &info) override;
	unique_ptr<HTTPResponse> Delete(DeleteRequestInfo &info) override;
	unique_ptr<HTTPResponse> Post(PostRequestInfo &info) override;

private:
	CURLRequestHeaders TransformHeadersCurl(const HTTPHeaders &header_map, const HTTPParams &params);
	void ResetRequestInfo();
	unique_ptr<HTTPResponse> TransformResponseCurl(CURLcode res);

	unique_ptr<CURLHandle> curl;
	optional_ptr<HTTPState> state;
	unique_ptr<RequestInfo> request_info;

	static void InitCurlGlobal();
	static void DestroyCurlGlobal();
};

} // namespace duckdb
