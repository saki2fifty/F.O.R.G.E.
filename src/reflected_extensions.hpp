#pragma once
#include <nlohmann/json.hpp>
namespace forge::detail {
// Requires a previously validated schema/value.
// Internal fragments only: never independently authored and never serialized.
// Structs keep unknown members; vectors with element_key keep fragments by that
// stable local string key. Other collections retain positional semantics.
nlohmann::json reflected_extensions(const nlohmann::json& schema, const nlohmann::json& value);
// Native known values always win. A removed element's extension never migrates
// to a different key; native collection order/count remains authoritative.
nlohmann::json merge_reflected_extensions(const nlohmann::json& schema, const nlohmann::json& known,
                                          const nlohmann::json& extensions);
} // namespace forge::detail
