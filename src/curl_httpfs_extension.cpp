#include "curl_httpfs_extension.hpp"

#include "duckdb.hpp"
#include "extension_loader_helper.hpp"
#include "hffs.hpp"
#include "httpfs_extension.hpp"
#include "multi_curl_util.hpp"
#include "s3fs.hpp"

namespace duckdb {

namespace {

// "httpfs" extension name.
constexpr const char *HTTPFS_EXTENSION = "httpfs";

// Whether `httpfs` extension has already been loaded.
bool IsHttpfsExtensionLoaded(DatabaseInstance &db_instance) {
	auto &extension_manager = db_instance.GetExtensionManager();
	const auto loaded_extensions = extension_manager.GetExtensions();
	return std::find(loaded_extensions.begin(), loaded_extensions.end(), HTTPFS_EXTENSION) != loaded_extensions.end();
}

// Ensure httpfs extension is loaded, loading it if necessary
void EnsureHttpfsExtensionLoaded(ExtensionLoader &loader, DatabaseInstance &instance) {
	const bool httpfs_extension_loaded = IsHttpfsExtensionLoaded(instance);
	if (httpfs_extension_loaded) {
		return;
	}
	auto httpfs_extension = make_uniq<HttpfsExtension>();
	httpfs_extension->Load(loader);

	// Register into extension manager to keep compatibility as httpfs.
	auto &extension_manager = ExtensionManager::Get(instance);
	auto extension_active_load = extension_manager.BeginLoad(HTTPFS_EXTENSION);
	// Manually fill in the extension install info to finalize extension load.
	ExtensionInstallInfo extension_install_info;
	extension_install_info.mode = ExtensionInstallMode::UNKNOWN;
	extension_active_load->FinishLoad(extension_install_info);
}

void LoadInternal(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &fs = instance.GetFileSystem();

	// To achieve full compatibility for duckdb-httpfs extension, all related functions/types/... should be supported,
	// so we load it first if not already loaded.
	EnsureHttpfsExtensionLoaded(loader, instance);

	// Override the default HTTP util to MultiCurlUtil for this extension.
	auto &config = DBConfig::GetConfig(instance);
	if (config.GetHTTPUtil().GetName() != "WasmHTTPUtils") {
		config.SetHTTPUtil(make_shared_ptr<MultiCurlUtil>());
	}

	// Register curl_httpfs-specific functions and settings.
	LoadExtensionInternal(loader);
}

} // namespace

void CurlHttpfsExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string CurlHttpfsExtension::Name() {
	return "curl_httpfs";
}

std::string CurlHttpfsExtension::Version() const {
#ifdef EXT_VERSION_HTTPFS
	return EXT_VERSION_HTTPFS;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(curl_httpfs, loader) {
	duckdb::LoadInternal(loader);
}
}
