#pragma once
#include <algorithm>
#include <cstddef>
#include <string>

namespace forge {
// Mirror of the runtime's kSdkPlatformAcksDiagnosticBytes (8 KiB).
// The editor's diagnostic fields ride the platform_acks batch and
// must stay under this cap; PlaySession::submit_sdk_platform_acks
// returns false when a diagnostic exceeds 8 KiB, so the helper
// bounds oversized text by replacing non-ASCII bytes with
// '?' (never splits a code point) and appending "...(truncated)"
// when truncation occurred.
inline constexpr std::size_t kBoundedDiagnosticBytes = 8 * 1024;
// Short valid messages pass through verbatim (with control
// whitespace normalized to a single space); only oversized messages
// get the ASCII-summary path. No Unicode framework.
inline std::string bound_diagnostic(const std::string& in) {
    constexpr std::size_t kReserve = 32;
    constexpr std::size_t kCap = kBoundedDiagnosticBytes > kReserve
                                     ? kBoundedDiagnosticBytes - kReserve
                                     : kBoundedDiagnosticBytes;
    if (in.size() <= kCap) {
        std::string out;
        out.reserve(in.size());
        for (unsigned char c : in)
            out.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : char(c));
        return out;
    }
    std::string out;
    out.reserve(kCap + 16);
    for (unsigned char c : in) {
        if (out.size() >= kCap)
            break;
        if (c == '\n' || c == '\r' || c == '\t')
            out.push_back(' ');
        else if (c < 0x80)
            out.push_back(char(c));
        else
            out.push_back('?');
    }
    out += "...(truncated)";
    return out;
}
} // namespace forge