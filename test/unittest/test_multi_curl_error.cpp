#include "catch.hpp"

#include <curl/curl.h>
#include <string_view>

#include "duckdb/main/http/http_util.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "http/httpfs_client.hpp"
#include "multi_curl_client.hpp"
#include "multi_curl_util.hpp"

using namespace duckdb;

namespace {

// Loopback port 1 is reserved and should have no listener — triggers CURLE_COULDNT_CONNECT quickly.
constexpr std::string_view UNREACHABLE_URL = "http://127.0.0.1:1/";

} // namespace

TEST_CASE("MultiCurlClient surfaces libcurl errors on connection failure", "[multi_curl][error]") {
	curl_global_init(CURL_GLOBAL_DEFAULT);

	MultiCurlUtil http_util;
	HTTPFSParams params(http_util);
	params.timeout = 1;
	params.enable_curl_server_cert_verification = false;

	MultiCurlClient client(params, "http://127.0.0.1:1");
	HTTPHeaders headers;
	GetRequestInfo request(string(UNREACHABLE_URL), headers, params, nullptr, nullptr);

	auto response = client.Get(request);
	REQUIRE(response != nullptr);
	REQUIRE(response->HasRequestError());
	REQUIRE_FALSE(response->GetRequestError().empty());
}

TEST_CASE("MultiCurlClient supports the upstream OPTIONS request path", "[multi_curl][error]") {
	MultiCurlUtil http_util;
	HTTPFSParams params(http_util);
	params.timeout = 1;
	params.enable_curl_server_cert_verification = false;
	MultiCurlClient client(params, "http://127.0.0.1:1");
	HTTPHeaders headers;
	OptionsRequestInfo request(string(UNREACHABLE_URL), headers, params);
	auto response = client.Options(request);
	REQUIRE(response != nullptr);
	REQUIRE(response->HasRequestError());
}

TEST_CASE("CurlRequest accepts empty and repeated header fields", "[multi_curl][headers]") {
	curl_global_init(CURL_GLOBAL_DEFAULT);
	CURLHandle handle(true);
	CurlRequest request(handle);
	string status = "HTTP/1.1 200 OK\r\n";
	string empty = "X-Empty:\r\n";
	string first = "Set-Cookie: a=1\r\n";
	string second = "Set-Cookie: b=2\r\n";
	CurlRequest::WriteHeader(status.data(), 1, status.size(), &request);
	CurlRequest::WriteHeader(empty.data(), 1, empty.size(), &request);
	CurlRequest::WriteHeader(first.data(), 1, first.size(), &request);
	CurlRequest::WriteHeader(second.data(), 1, second.size(), &request);
	const auto &headers = request.info->header_collection.back();
	REQUIRE(headers.GetHeaderValue("X-Empty").empty());
	REQUIRE(headers.GetHeaderValues("Set-Cookie").size() == 2);
}
