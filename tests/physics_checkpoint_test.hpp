#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace forge::test {
// Recompute transport corruption detection so malformed payloads reach admission.
inline void reseal_physics_checkpoint(nlohmann::json& value) {
    value.erase("integrity");
    std::uint64_t checksum = 14695981039346656037ull;
    for (unsigned char byte : value.dump()) {
        checksum ^= byte;
        checksum *= 1099511628211ull;
    }
    value["integrity"] = std::to_string(checksum);
}
} // namespace forge::test
