#pragma once
#include <algorithm>
#include <cstddef>
#include <string_view>
namespace forge::detail {
// This preserves a range boundary; it is not UTF-8 validation or replacement.
// Callers retain their admission policy, byte budget and ownership.
inline std::size_t utf8_range_end(std::string_view text, std::size_t start, std::size_t end) {
    end = std::min(end, text.size());
    while (end > start && end < text.size() &&
           (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    return end;
}
} // namespace forge::detail
