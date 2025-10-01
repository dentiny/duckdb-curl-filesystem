#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <cassert>
#include <curl/curl.h>
#include <iostream>

#include "duckdb.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/http_util.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "multi_curl_manager.hpp"
#include "httpfs.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/client_context_file_opener.hpp"
#include "duckdb/main/client_context.hpp"

using namespace duckdb;

TEST_CASE("Range read test", "[filesystem test]") {
	DuckDB db(nullptr);
	auto &instance = db.instance;
	instance->config.http_util = make_shared_ptr<HTTPFSCurlUtil>();
	auto client_context = make_shared_ptr<ClientContext>(instance);
	client_context->transaction.BeginTransaction();
	ClientContextFileOpener file_opener {*client_context};

	HTTPFileSystem fs {};
	string url = "https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv";
	auto file_handle = fs.OpenFile(url, FileOpenFlags::FILE_FLAGS_READ, &file_opener);
	const idx_t file_size = file_handle->GetFileSize();
	REQUIRE(file_size == 16222);

	// Perform range read.
	const idx_t bytes_to_read = 54; // number of characters in the first line
	string buffer(bytes_to_read, '\0');
	file_handle->Read(static_cast<void *>(const_cast<char *>(buffer.data())), bytes_to_read, /*location=*/0);
	REQUIRE(buffer == "csvbase_row_id,Continent,Country,Name,MIC,Last changed");
}

int main(int argc, char **argv) {
	int result = Catch::Session().run(argc, argv);
	return result;
}
