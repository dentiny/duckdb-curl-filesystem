#pragma once

#include "httpfs_client.hpp"
#include "multi_curl_client.hpp"

namespace duckdb {

class MultiCurlUtil : public HTTPFSCurlUtil {
public:
	unique_ptr<HTTPClient> InitializeClient(HTTPParams &http_params, const string &proto_host_port) override {
		auto client = make_uniq<MultiCurlClient>(http_params.Cast<HTTPFSParams>(), proto_host_port);
		return std::move(client);
	}

	string GetName() const override {
		return "MultiCurl";
	}
};

} // namespace duckdb
