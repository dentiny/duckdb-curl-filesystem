// This benchmark compares the performance under heavy load read request, between httplib and multi-curl based
// implementation.

#include <array>
#include <chrono>

#include "duckdb.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/http_util.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/shared_ptr.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_file_opener.hpp"
#include "httpfs.hpp"
#include "httpfs_client.hpp"
#include "multi_curl_manager.hpp"
#include "thread_pool.hpp"

using namespace duckdb; // NOLINT

// Spawn a number of threads for concurrent IO requests.
constexpr size_t CONCURRENCY = 2000;
// Test block sizes.
constexpr std::array<idx_t, 7> TEST_BLOCK_SIZES = {16, 128, 1024, 8 * 1024, 64 * 1024, 512 * 1024, 2 * 1024 * 1024};

// Handle single IO request: one HEAD request + one GET request.
void HandleSingleRequest(ClientContextFileOpener *file_opener, idx_t start_offset, idx_t bytes_to_read,
                         idx_t file_size) {
	HTTPFileSystem fs {};
	string url = "https://raw.githubusercontent.com/dentiny/duck-read-cache-fs/main/test/data/stock-exchanges.csv";
	auto file_handle = fs.OpenFile(url, FileOpenFlags::FILE_FLAGS_READ, file_opener);

	bytes_to_read = std::min(bytes_to_read, file_size - start_offset);
	string buffer(bytes_to_read, '\0');
	file_handle->Read(static_cast<void *>(const_cast<char *>(buffer.data())), bytes_to_read, /*location=*/start_offset);
}

// Read a remote object via httpfs.
void PerformBenchmarkImpl(shared_ptr<HTTPFSUtil> httpfs_util, idx_t block_size) {
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

	ThreadPool tp {CONCURRENCY};
	for (idx_t cur_start_offset = 0; cur_start_offset < file_size; cur_start_offset += block_size) {
		tp.Push([cur_start_offset, &file_opener, block_size, file_size]() {
			HandleSingleRequest(&file_opener, cur_start_offset, block_size, file_size);
		});
	}
	tp.Wait();
}

// Benchmark httplib-based http filesystem.
void BenchmarkHttplib(idx_t block_size) {
	const auto start = std::chrono::steady_clock::now();
	auto http_util = make_shared_ptr<HTTPFSUtil>();
	PerformBenchmarkImpl(std::move(http_util), block_size);
	const auto end = std::chrono::steady_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	std::cout << "httplib-based IO operation with block size " << block_size << " takes " << duration.count()
	          << " milliseconds" << std::endl;
}

// Benchmark multi-curl-based http filesystem.
void BenchmarkMultiCurl(idx_t block_size) {
	const auto start = std::chrono::steady_clock::now();
	auto http_util = make_shared_ptr<HTTPFSCurlUtil>();
	PerformBenchmarkImpl(std::move(http_util), block_size);
	const auto end = std::chrono::steady_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	std::cout << "multi-curl-based IO operation with block size " << block_size << " takes " << duration.count()
	          << " milliseconds" << std::endl;
}

int main() {
	for (idx_t cur_block_size : TEST_BLOCK_SIZES) {
		BenchmarkHttplib(cur_block_size);
		BenchmarkMultiCurl(cur_block_size);
	}
	return 0;
}
