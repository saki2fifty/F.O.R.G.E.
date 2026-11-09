#pragma once
#include <cstddef>
#include <span>
#include <string_view>

namespace forge::ui_font_detail {
inline constexpr unsigned collection_profile_version = 1;
inline constexpr unsigned face_limit = 32;
inline constexpr std::size_t file_byte_limit = 4 * 1024 * 1024;
inline constexpr bool extension(std::string_view value) {
    return value == ".ttf" || value == ".otf" || value == ".ttc" || value == ".otc";
}
// The existing UI asset admission owner validates every directory and returns
// the number of addressable faces. It does not decode glyphs or retain bytes.
unsigned face_count(std::span<const std::byte>);
} // namespace forge::ui_font_detail
