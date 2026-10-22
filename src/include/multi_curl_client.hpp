#pragma once

#include "duckdb/main/http/http_util.hpp"
#include "http/httpfs_client.hpp"
#include "http/httpfs_curl_client.hpp"
#include "http/http_state.hpp"
#include "curl_request.hpp"

namespace duckdb {

class MultiCurlClient : public HTTPClient {
public:
	MultiCurlClient(HTTPFSParams &http_params, const string &proto_host_port);
	~MultiCurlClient() override = default;

	void Initialize(HTTPParams &http_params) override;

	unique_ptr<HTTPResponse> Get(GetRequestInfo &info) override;
	unique_ptr<HTTPResponse> Put(PutRequestInfo &info) override;
	unique_ptr<HTTPResponse> Head(HeadRequestInfo &info) override;
	unique_ptr<HTTPResponse> Delete(DeleteRequestInfo &info) override;
	unique_ptr<HTTPResponse> Post(PostRequestInfo &info) override;
	unique_ptr<HTTPResponse> Options(OptionsRequestInfo &info) override;

private:
	shared_ptr<const string> ca_cert_bundle;
	unique_ptr<CURLHandle> curl;
	shared_ptr<HTTPState> state;
	unique_ptr<HTTPClient> upstream_client;
};

} // namespace duckdb
