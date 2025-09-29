#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <curl/curl.h>
#include <iostream>

#include "duckdb/common/unique_ptr.hpp"
#include "multi_curl_manager.hpp"

using namespace duckdb;

namespace {
size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::string*>(userdata);
    buffer->append(ptr, size * nmemb);
    return size * nmemb;
}
}  // namespace

TEST_CASE("multi-curl test", "[curl test]") {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    string url = "https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv";
    auto request = make_uniq<EasyRequest>(std::move(url));

    auto& multi_curl_manager = MultiCurlManager::GetInstance();    
    auto response = multi_curl_manager.HandleRequest(std::move(request));

    std::cout << "Response body = " << response->body << std::endl;
}

int main(int argc, char **argv) {
	int result = Catch::Session().run(argc, argv);
	return result;
}
