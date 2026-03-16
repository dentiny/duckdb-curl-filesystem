#pragma once

#include "httpfs_client.hpp"

namespace duckdb {

class MultiCurlUtil : public HTTPFSCurlUtil {
public:
	unique_ptr<HTTPClient> InitializeClient(HTTPParams &http_params, const string &proto_host_port) override;
	string GetName() const override;
};

} // namespace duckdb
