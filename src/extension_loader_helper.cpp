#include "extension_loader_helper.hpp"

#include "curl_httpfs_functions.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "extension_config.hpp"
#include "httpfs_client.hpp"
#include "multi_curl_util.hpp"

namespace duckdb {

void LoadExtensionInternal(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(instance);

	// Select the HTTP client implementation. Extends upstream httpfs with multi_curl support.
	auto callback_httpfs_client_implementation = [](ClientContext &context, SetScope scope, Value &parameter) {
		auto &config = DBConfig::GetConfig(context);
		string value = StringValue::Get(parameter);
		if (config.GetHTTPUtil().GetName() == "WasmHTTPUtils") {
			if (value == "wasm" || value == "default") {
				return;
			}
			throw InvalidInputException("Unsupported option for curl_httpfs_client_implementation, only `wasm` and "
			                            "`default` are currently supported for duckdb-wasm");
		}
		if (value == "multi_curl" || value == "default") {
			if (config.GetHTTPUtil().GetName() != "MultiCurl") {
				config.SetHTTPUtil(make_shared_ptr<MultiCurlUtil>());
			}
			return;
		}
		if (value == "curl") {
			if (config.GetHTTPUtil().GetName() != "HTTPFSUtil-Curl") {
				config.SetHTTPUtil(make_shared_ptr<HTTPFSCurlUtil>());
			}
			return;
		}
		if (value == "httplib") {
			if (config.GetHTTPUtil().GetName() != "HTTPFSUtil") {
				config.SetHTTPUtil(make_shared_ptr<HTTPFSUtil>());
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

	RegisterCurlHttpfsFunctions(loader);
}

} // namespace duckdb
