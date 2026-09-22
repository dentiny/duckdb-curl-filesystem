#include "curl_httpfs_functions.hpp"

#include <utility>

#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "httpfs_client.hpp"
#include "tcp_connection_query_function.hpp"

namespace duckdb {

namespace {

FunctionDescription MakeFunctionDescription(vector<string> parameter_names, string description, vector<string> examples,
                                            vector<string> categories) {
	FunctionDescription function_description;
	function_description.parameter_names = std::move(parameter_names);
	function_description.description = std::move(description);
	function_description.examples = std::move(examples);
	function_description.categories = std::move(categories);
	return function_description;
}

void RegisterScalarFunction(ExtensionLoader &loader, ScalarFunction function, vector<string> parameter_names,
                            string description, vector<string> examples, vector<string> categories) {
	CreateScalarFunctionInfo info(std::move(function));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	info.descriptions.push_back(MakeFunctionDescription(std::move(parameter_names), std::move(description),
	                                                    std::move(examples), std::move(categories)));
	loader.RegisterFunction(std::move(info));
}

void RegisterTableFunction(ExtensionLoader &loader, TableFunction function, vector<string> parameter_names,
                           string description, vector<string> examples, vector<string> categories) {
	CreateTableFunctionInfo info(std::move(function));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	info.descriptions.push_back(MakeFunctionDescription(std::move(parameter_names), std::move(description),
	                                                    std::move(examples), std::move(categories)));
	loader.RegisterFunction(std::move(info));
}

void GetHttpUtilName(const DataChunk &args, ExpressionState &state, Vector &result) {
	auto &config = DBConfig::GetConfig(*state.GetContext().db);
	result.Reference(Value(config.GetHTTPUtil().GetName()));
}

} // namespace

void RegisterCurlHttpfsFunctions(ExtensionLoader &loader) {
	ScalarFunction http_util_name_function("curl_httpfs_http_util_name", /*arguments=*/ {},
	                                       /*return_type=*/LogicalType {LogicalTypeId::VARCHAR}, GetHttpUtilName);
	RegisterScalarFunction(
	    loader, std::move(http_util_name_function),
	    /*parameter_names=*/ {},
	    /*description=*/"Returns the name of the HTTP client implementation currently used by curl_httpfs.",
	    /*examples=*/ {"SELECT curl_httpfs_http_util_name();"},
	    /*categories=*/ {"http", "diagnostics"});

	RegisterTableFunction(loader, GetTcpConnectionNumFunc(),
	                      /*parameter_names=*/ {},
	                      /*description=*/"Returns active system TCP connection counts grouped by remote IP address.",
	                      /*examples=*/ {"SELECT * FROM curl_httpfs_get_tcp_connection();"},
	                      /*categories=*/ {"http", "network"});
}

} // namespace duckdb
