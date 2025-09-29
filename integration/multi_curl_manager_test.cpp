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

    auto request = make_uniq<EasyRequest>();
    request->info->url = "https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv";
    auto response = MultiCurlManager::GetInstance().HandleRequest(std::move(request));

    std::cout << "Reponse body = " << response->body << std::endl;
}

int main(int argc, char **argv) {
	int result = Catch::Session().run(argc, argv);
	return result;
}
