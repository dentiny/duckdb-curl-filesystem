// This file contails curl-based http util config.

#pragma once

#include <atomic>
#include <string_view>

namespace duckdb {

inline constexpr std::string_view MULTI_CURL_NAME = "MultiCurl";
inline constexpr std::string_view CURL_UTIL_NAME = "HTTPFS-Curl";
inline constexpr std::string_view HTTPLIB_UTIL_NAME = "HTTPFS";
inline constexpr std::string_view WASM_UTIL_NAME = "WasmHTTPUtils";
inline constexpr std::string_view MULTI_CURL_IMPLEMENTATION = "multi_curl";
inline constexpr std::string_view CURL_IMPLEMENTATION = "curl";
inline constexpr std::string_view HTTPLIB_IMPLEMENTATION = "httplib";
inline constexpr std::string_view WASM_IMPLEMENTATION = "wasm";
inline constexpr std::string_view DEFAULT_IMPLEMENTATION = "default";
inline constexpr std::string_view RESPONSE_STATUS_HEADER = "__RESPONSE_STATUS__";
inline constexpr std::string_view HTTP_STATUS_PREFIX = "HTTP/";

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
