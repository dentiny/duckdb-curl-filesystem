// This file contails curl-based http util config.

#pragma once

#include <atomic>

namespace duckdb {

//===--------------------------------------------------------------------===//
// Default configuration
//===--------------------------------------------------------------------===//

inline constexpr bool DEFAULT_CURL_VERBOSE_LOGGING = false;

//===--------------------------------------------------------------------===//
// Global configuration
//===--------------------------------------------------------------------===//

// Whether to enable verbose logging for curl-based http util.
inline std::atomic<bool> ENABLE_CURL_VERBOSE_LOGGING {DEFAULT_CURL_VERBOSE_LOGGING};

} // namespace duckdb
