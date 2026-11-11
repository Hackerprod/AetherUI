#pragma once
#include "model.hpp"
#include <string_view>
#include <map>
namespace avc {
struct Json {
 enum class Kind { object,array,string,number,boolean,null_value } kind;
 std::map<std::string,Json> object;
 std::vector<Json> array;
 std::string scalar;
 const Json* get(std::string_view key) const;
};
std::optional<Json> parse_json(std::string_view);
bool valid_utf8(std::string_view);
bool valid_resource_path(std::string_view);
// Strict UTF-8 JSON, bounded before and during parsing. No I/O.
std::optional<Manifest> parse_manifest(std::string_view);
}
