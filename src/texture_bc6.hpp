#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace forge::asset_detail {
// A single standard BC6H block, RGB binary16 words in row-major order.
// Reserved modes produce black; signed negative infinity is preserved here.
std::array<std::uint16_t, 48> decode_bc6_block(std::span<const std::byte, 16>,
                                               bool signed_samples) noexcept;
} // namespace forge::asset_detail
