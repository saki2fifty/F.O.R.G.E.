#pragma once
#include <map>
#include <nlohmann/json.hpp>
#include <string>
namespace forge::detail {
// Temporary borrowed lookup for one immutable document operation. The source
// rows retain ownership; no cache, identity allocation or document mutation.
using JsonRowIndex = std::map<std::string, const nlohmann::json*>;
inline JsonRowIndex index_json_rows(const nlohmann::json& rows) {
    JsonRowIndex index;
    for (const auto& row : rows)
        index.emplace(row.at("id").get_ref<const std::string&>(), &row);
    return index;
}
} // namespace forge::detail
