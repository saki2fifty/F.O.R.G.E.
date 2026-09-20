#pragma once
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>

namespace forge::asset_detail {
// nlohmann/json 3.12 callback depth: object_start/end use the containing
// depth; key uses containing depth + 1. Reject duplicate keys before DOM
// insertion so different downstream parsers cannot select different values.
inline nlohmann::json parse_bounded_json(std::span<const std::byte> bytes, std::size_t byte_limit,
                                         std::size_t event_limit = 4000000, int depth_limit = 64) {
    if (bytes.size() > byte_limit)
        throw std::runtime_error("Asset JSON exceeds byte limit");
    std::size_t events = 0;
    std::map<int, std::set<std::string>> keys;
    using Json = nlohmann::json;
    return Json::parse(bytes.begin(), bytes.end(),
                       [&](int depth, Json::parse_event_t event, Json& value) {
                           if (depth > depth_limit || ++events > event_limit)
                               throw std::runtime_error("Asset JSON exceeds structural limits");
                           if (event == Json::parse_event_t::object_start)
                               keys[depth + 1].clear();
                           else if (event == Json::parse_event_t::object_end)
                               keys.erase(depth + 1);
                           else if (event == Json::parse_event_t::key &&
                                    !keys.at(depth).insert(value.get<std::string>()).second)
                               throw std::runtime_error("Duplicate field in asset JSON");
                           return true;
                       });
}
} // namespace forge::asset_detail
