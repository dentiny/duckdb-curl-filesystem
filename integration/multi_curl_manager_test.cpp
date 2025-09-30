#include <curl/curl.h>
#include <iostream>

#include "duckdb/common/helper.hpp"
#include "duckdb/common/http_util.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "multi_curl_manager.hpp"

using namespace duckdb;

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    string url = "https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv";
    auto req = make_uniq<CurlRequest>(std::move(url));

    auto &mgr = MultiCurlManager::GetInstance();
    auto resp = mgr.HandleRequest(std::move(req));

    std::cout << "Response (" << HTTPUtil::GetStatusMessage(resp->status) << "):\n"
              << resp->body << "...\n";

    return 0;
}
