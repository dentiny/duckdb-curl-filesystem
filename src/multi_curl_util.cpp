#include "multi_curl_util.hpp"

#include "multi_curl_client.hpp"
#include "extension_config.hpp"

namespace duckdb {

unique_ptr<HTTPClient> MultiCurlUtil::InitializeClient(HTTPParams &http_params, const string &proto_host_port) {
	auto client = make_uniq<MultiCurlClient>(http_params.Cast<HTTPFSParams>(), proto_host_port);
	return client;
}

string MultiCurlUtil::GetName() const {
	return string(MULTI_CURL_NAME);
}

HTTPTransportReusePolicy MultiCurlUtil::GetTransportReusePolicy() const {
	return HTTPTransportReusePolicy::EPHEMERAL;
}

} // namespace duckdb
