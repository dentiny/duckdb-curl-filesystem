#include <curl/curl.h>
#include <iostream>

#include "duckdb/common/unique_ptr.hpp"
#include "multi_curl_manager.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/http_util.hpp"

using namespace duckdb;

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    auto req = make_uniq<CurlRequest>(
        "https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv"
    );

    auto &mgr = MultiCurlManager::GetInstance();
    auto resp = mgr.HandleRequest(std::move(req));

    std::cout << "Response (" << HTTPUtil::GetStatusMessage(resp->status) << "):\n"
              << resp->body << "...\n";

    return 0;
}
