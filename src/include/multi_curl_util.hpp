#pragma once

#include "http/httpfs_client.hpp"

namespace duckdb {

class MultiCurlUtil : public HTTPFSCurlUtil {
public:
	using HTTPFSCurlUtil::HTTPFSCurlUtil;

	unique_ptr<HTTPClient> InitializeClient(HTTPParams &http_params, const string &proto_host_port) override;
	string GetName() const override;
	HTTPTransportReusePolicy GetTransportReusePolicy() const override;
};

} // namespace duckdb
