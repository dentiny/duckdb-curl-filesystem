#include "extension_loader.hpp"

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

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
}

}  // namespace duckdb
