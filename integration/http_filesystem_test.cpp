#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <cassert>
#include <curl/curl.h>
#include <thread>

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
#include "duckdb/common/vector.hpp"

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

TEST_CASE("Concurrent range read test", "[filesystem test]") {
	DuckDB db(nullptr);
	auto &instance = db.instance;
	instance->config.http_util = make_shared_ptr<HTTPFSCurlUtil>();
	auto client_context = make_shared_ptr<ClientContext>(instance);
	client_context->transaction.BeginTransaction();
	ClientContextFileOpener file_opener {*client_context};

	HTTPFileSystem fs {};
	string url = "https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv";
	auto file_handle =
	    fs.OpenFile(url, FileOpenFlags::FILE_FLAGS_READ | FileOpenFlags::FILE_FLAGS_PARALLEL_ACCESS, &file_opener);
	const idx_t file_size = file_handle->GetFileSize();
	REQUIRE(file_size == 16222);

	// Perform concurrent range read.
	const idx_t bytes_to_read = 54; // number of characters in the first line
	string buffer(bytes_to_read, '\0');
	vector<std::thread> read_threads(bytes_to_read); // one thread only reads one byte
	for (int idx = 0; idx < bytes_to_read; ++idx) {
		read_threads[idx] = std::thread([&file_handle, &buffer, idx]() {
			void *ptr = static_cast<void *>(const_cast<char *>(buffer.data() + idx));
			file_handle->Read(ptr, /*nr_bytes=*/1, /*location=*/idx);
		});
	}
	for (auto &cur_thd : read_threads) {
		REQUIRE(cur_thd.joinable());
		cur_thd.join();
	}
	REQUIRE(buffer == "csvbase_row_id,Continent,Country,Name,MIC,Last changed");
}

int main(int argc, char **argv) {
	int result = Catch::Session().run(argc, argv);
	return result;
}
