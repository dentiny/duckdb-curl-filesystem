#include "catch.hpp"

#include <curl/curl.h>

#include "duckdb/common/http_util.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "httpfs_client.hpp"
#include "multi_curl_client.hpp"
#include "multi_curl_util.hpp"

using namespace duckdb;

namespace {

// Loopback port 1 is reserved and should have no listener — triggers CURLE_COULDNT_CONNECT quickly.
constexpr const char *UNREACHABLE_URL = "http://127.0.0.1:1/";

} // namespace

TEST_CASE("MultiCurlClient surfaces libcurl errors on connection failure", "[multi_curl][error]") {
	curl_global_init(CURL_GLOBAL_DEFAULT);

	MultiCurlUtil http_util;
	HTTPFSParams params(http_util);
	params.timeout = 1;
	params.enable_curl_server_cert_verification = false;

	MultiCurlClient client(params, "http://127.0.0.1:1");
	HTTPHeaders headers;
	GetRequestInfo request(UNREACHABLE_URL, headers, params, nullptr, nullptr);

	auto response = client.Get(request);
	REQUIRE(response != nullptr);
	REQUIRE(response->HasRequestError());
	REQUIRE_FALSE(response->GetRequestError().empty());
}
