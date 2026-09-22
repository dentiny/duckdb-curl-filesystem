#pragma once

namespace duckdb {

class ExtensionLoader;

void RegisterCurlHttpfsFunctions(ExtensionLoader &loader);

} // namespace duckdb
