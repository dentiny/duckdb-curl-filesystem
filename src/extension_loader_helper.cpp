#include "extension_loader_helper.hpp"

#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "extension_config.hpp"
#include "httpfs_client.hpp"
#include "multi_curl_util.hpp"
#include "tcp_connection_query_function.hpp"
#include "tcp_ip_recorder.hpp"

namespace duckdb {

namespace {
constexpr bool SUCCESS = true;

// Clear internal metrics collection.
void ClearAllCache(const DataChunk &args, ExpressionState &state, Vector &result) {
	TcpIpRecorder::GetInstance().Clear();
	result.Reference(Value(SUCCESS));
}

// Get the name of the active HTTP util implementation.
void GetHttpUtilName(const DataChunk &args, ExpressionState &state, Vector &result) {
	auto &config = DBConfig::GetConfig(*state.GetContext().db);
	string name = config.http_util ? config.http_util->GetName() : "(None)";
	result.Reference(Value(name));
}
} // namespace

void LoadExtensionInternal(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(instance);

	// Expose the active HTTP util implementation name for diagnostics and testing.
	ScalarFunction http_util_name_function("curl_httpfs_http_util_name", /*arguments=*/ {},
	                                       /*return_type=*/LogicalType {LogicalTypeId::VARCHAR}, GetHttpUtilName);
	loader.RegisterFunction(std::move(http_util_name_function));

	// Select the HTTP client implementation. Extends upstream httpfs with multi_curl support.
	auto callback_httpfs_client_implementation = [](ClientContext &context, SetScope scope, Value &parameter) {
		auto &config = DBConfig::GetConfig(context);
		string value = StringValue::Get(parameter);
		if (config.http_util && config.http_util->GetName() == "WasmHTTPUtils") {
			if (value == "wasm" || value == "default") {
				return;
			}
			throw InvalidInputException("Unsupported option for curl_httpfs_client_implementation, only `wasm` and "
			                            "`default` are currently supported for duckdb-wasm");
		}
		if (value == "multi_curl" || value == "default") {
			if (!config.http_util || config.http_util->GetName() != "MultiCurl") {
				config.http_util = make_shared_ptr<MultiCurlUtil>();
			}
			return;
		}
		if (value == "curl") {
			if (!config.http_util || config.http_util->GetName() != "HTTPFSUtil-Curl") {
				config.http_util = make_shared_ptr<HTTPFSCurlUtil>();
			}
			return;
		}
		if (value == "httplib") {
			if (!config.http_util || config.http_util->GetName() != "HTTPFSUtil") {
				config.http_util = make_shared_ptr<HTTPFSUtil>();
			}
			return;
		}
		throw InvalidInputException("Unsupported option for curl_httpfs_client_implementation, only `multi_curl`, "
		                            "`curl`, `httplib` and `default` are currently supported");
	};
	config.AddExtensionOption("curl_httpfs_client_implementation",
	                          "Select the HTTPUtil implementation to be used. "
	                          "Supports `multi_curl`, `curl`, `httplib`, and `default` (multi_curl).",
	                          LogicalType {LogicalTypeId::VARCHAR}, "default",
	                          std::move(callback_httpfs_client_implementation));

	// Provide option to enable verbose logging for curl-based implementation.
	auto callback_set_curl_verbose_logging = [](ClientContext &context, SetScope scope, Value &parameter) {
		ENABLE_CURL_VERBOSE_LOGGING = parameter.GetValue<bool>();
	};
	config.AddExtensionOption("curl_httpfs_enable_verbose_logging",
	                          "Turn on and off curl-based http util verbose logging.", LogicalType::BOOLEAN, false,
	                          callback_set_curl_verbose_logging);

	// Clear extension internal metrics collection.
	ScalarFunction clear_cache_function("curl_httpfs_clear_metrics", /*arguments=*/ {},
	                                    /*return_type=*/LogicalType {LogicalTypeId::BOOLEAN}, ClearAllCache);
	loader.RegisterFunction(std::move(clear_cache_function));

	// Register TCP connection status function.
	loader.RegisterFunction(GetTcpConnectionNumFunc());

	// Register httpfs TCP connection status function.
	loader.RegisterFunction(GetHttpfsTcpConnectionNumFunc());
}

} // namespace duckdb
