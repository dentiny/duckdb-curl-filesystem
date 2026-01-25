#include "extension_loader_helper.hpp"

#include "duckdb/main/extension/extension_loader.hpp"
#include "extension_config.hpp"
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
} // namespace

void LoadExtensionInternal(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(instance);

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
	loader.RegisterFunction(clear_cache_function);

	// Register TCP connection status function.
	loader.RegisterFunction(GetTcpConnectionNumFunc());

	// Register httpfs TCP connection status function.
	loader.RegisterFunction(GetHttpfsTcpConnectionNumFunc());
}

} // namespace duckdb
