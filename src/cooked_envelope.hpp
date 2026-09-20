#pragma once
#include "bounded_json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
namespace forge::asset_detail {
inline void cooked_write32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(std::byte((value >> (i * 8)) & 255));
}
inline std::uint32_t cooked_read32(std::span<const std::byte> bytes, std::size_t& at) {
    if (at > bytes.size() || bytes.size() - at < 4)
        throw std::runtime_error("Truncated cooked scalar");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[at++]) << (i * 8);
    return value;
}
struct CookedEnvelope {
    nlohmann::json metadata;
    std::span<const std::byte> payload;
};
inline CookedEnvelope decode_envelope(std::span<const std::byte> bytes,
                                      const std::array<std::byte, 8>& magic,
                                      std::size_t metadata_limit, std::size_t payload_limit) {
    if (bytes.size() < 24 || !std::equal(magic.begin(), magic.end(), bytes.begin()))
        throw std::runtime_error("Invalid cooked asset magic/header");
    std::size_t at = 8;
    if (cooked_read32(bytes, at) != 1)
        throw std::runtime_error("Unsupported cooked asset version");
    const auto json_size = cooked_read32(bytes, at), payload_size = cooked_read32(bytes, at);
    if (cooked_read32(bytes, at) != 0 || json_size > metadata_limit ||
        payload_size > payload_limit || json_size > bytes.size() - 24 ||
        bytes.size() - 24 - json_size != payload_size)
        throw std::runtime_error("Invalid cooked asset lengths/reserved field");
    return {parse_bounded_json(bytes.subspan(24, json_size), metadata_limit),
            bytes.subspan(24 + json_size)};
}
inline std::vector<std::byte> encode_envelope(const nlohmann::json& metadata,
                                              std::span<const std::byte> payload,
                                              const std::array<std::byte, 8>& magic,
                                              std::size_t metadata_limit) {
    const auto json = metadata.dump();
    if (json.size() > metadata_limit || json.size() > UINT32_MAX || payload.size() > UINT32_MAX)
        throw std::runtime_error("Cooked asset exceeds envelope");
    std::vector<std::byte> out(magic.begin(), magic.end());
    cooked_write32(out, 1);
    cooked_write32(out, static_cast<std::uint32_t>(json.size()));
    cooked_write32(out, static_cast<std::uint32_t>(payload.size()));
    cooked_write32(out, 0);
    out.insert(out.end(), reinterpret_cast<const std::byte*>(json.data()),
               reinterpret_cast<const std::byte*>(json.data() + json.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
} // namespace forge::asset_detail
