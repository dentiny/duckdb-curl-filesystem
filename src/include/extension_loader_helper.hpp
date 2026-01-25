#pragma once

namespace duckdb {

// Forward declaration.
class ExtensionLoader;

// Loads curl-httpfs extension specific settings and functions.
void LoadExtensionInternal(ExtensionLoader &loader);

}  // namespace duckdb
