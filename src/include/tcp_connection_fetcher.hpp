#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/unordered_map.hpp"

namespace duckdb {

// Get TCP connection number grouped by IP.
unordered_map<string, int> GetTcpConnectionNum();

// Get TCP connection number by IP used by httpfs.
unordered_map<string, int> GetHttpfsTcpConnectionNum();

} // namespace duckdb
