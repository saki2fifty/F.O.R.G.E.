#include <forge/identity.hpp>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#else
#include <cerrno>
#include <sys/random.h>
#endif
namespace forge::detail {
UuidBytes uuid_v4() {
    UuidBytes bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Cannot obtain system randomness for persistent identity");
#else
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto n = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            throw std::runtime_error("Cannot obtain system randomness for persistent identity");
        offset += static_cast<std::size_t>(n);
    }
#endif
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    return bytes;
}
std::string uuid_text(const UuidBytes& bytes) {
    std::string result;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            result += '-';
        result += "0123456789abcdef"[bytes[i] >> 4];
        result += "0123456789abcdef"[bytes[i] & 15];
    }
    return result;
}
UuidBytes parse_uuid(std::string_view text) {
    auto bad = [] { throw std::runtime_error("Expected a canonical lowercase UUIDv4"); };
    if (text.size() != 36)
        bad();
    UuidBytes bytes{};
    std::size_t cursor = 0;
    auto nibble = [&](char c) -> unsigned {
        if (c >= '0' && c <= '9')
            return unsigned(c - '0');
        if (c >= 'a' && c <= 'f')
            return unsigned(c - 'a' + 10);
        bad();
        return 0;
    };
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            if (text[cursor++] != '-')
                bad();
        const auto high = nibble(text[cursor++]);
        bytes[i] = static_cast<std::uint8_t>((high << 4) | nibble(text[cursor++]));
    }
    if ((bytes[6] >> 4) != 4 || (bytes[8] & 0xc0) != 0x80)
        bad();
    return bytes;
}
} // namespace forge::detail
