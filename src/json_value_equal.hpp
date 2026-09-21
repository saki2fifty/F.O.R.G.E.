#pragma once
#include <nlohmann/json.hpp>
namespace forge::detail {
// Authored-value equality must not narrow an unsigned integer to int64, or
// compare it through double. nlohmann/json's mixed numeric operator== does both.
inline bool json_value_equal(const nlohmann::json& a, const nlohmann::json& b) {
    using Json = nlohmann::json;
    if (a.type() != b.type()) {
        if (a.type() == Json::value_t::number_integer && b.is_number_unsigned()) {
            const auto value = a.get<std::int64_t>();
            return value >= 0 && std::uint64_t(value) == b.get<std::uint64_t>();
        }
        if (b.type() == Json::value_t::number_integer && a.is_number_unsigned())
            return json_value_equal(b, a);
        return false;
    }
    if (a.is_structured()) {
        if (a.size() != b.size())
            return false;
        auto other = b.begin();
        for (auto item = a.begin(); item != a.end(); ++item, ++other)
            if ((a.is_object() && item.key() != other.key()) ||
                !json_value_equal(item.value(), other.value()))
                return false;
        return true;
    }
    return a == b;
}
} // namespace forge::detail
