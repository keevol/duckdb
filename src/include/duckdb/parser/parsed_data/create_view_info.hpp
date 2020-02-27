//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/parser/parsed_data/create_view_info.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/parser/parsed_data/create_info.hpp"
#include "duckdb/parser/query_node.hpp"

namespace duckdb {

struct CreateViewInfo : public CreateInfo {
	CreateViewInfo() : CreateInfo(CatalogType::VIEW) {
	}
	CreateViewInfo(string schema, string view_name) : CreateInfo(CatalogType::VIEW, schema), view_name(view_name) {
	}

	//! Schema name to insert to
	string schema;
	//! Table name to insert to
	string view_name;
	//! Aliases of the
	vector<string> aliases;
	//! The QueryNode of the view
	unique_ptr<QueryNode> query;
};

} // namespace duckdb
