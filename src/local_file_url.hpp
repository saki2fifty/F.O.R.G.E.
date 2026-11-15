#pragma once
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace forge {
inline std::string local_file_url(const std::filesystem::path& path) {
    const auto text = std::filesystem::absolute(path).generic_u8string();
    std::string result = text.starts_with(u8"/") ? "file://" : "file:///";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto value : text) {
        const auto byte = static_cast<unsigned char>(value);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '/' || byte == ':' || byte == '-' ||
            byte == '_' || byte == '.' || byte == '~')
            result += static_cast<char>(byte);
        else {
            result += '%';
            result += hex[byte >> 4];
            result += hex[byte & 15];
        }
    }
    return result;
}
// Decode only a file URL. Callers retain their own project/SDK containment rules.
inline std::filesystem::path local_file_url_path(std::string_view url) {
    if (!url.starts_with("file://") || url.find_first_of("?#") != url.npos)
        throw std::runtime_error("Expected a file URL without a query or fragment");
    const auto rest = url.substr(7);
    const auto slash = rest.find('/');
    if (slash == rest.npos)
        throw std::runtime_error("File URL needs an absolute path");
    const auto decoded = [](std::string_view encoded) {
        std::string text;
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            auto byte = encoded[i];
            if (byte == '%') {
                if (encoded.size() - i < 3)
                    throw std::runtime_error("Incomplete file URL escape");
                unsigned value = 0;
                const auto parsed =
                    std::from_chars(encoded.data() + i + 1, encoded.data() + i + 3, value, 16);
                if (parsed.ec != std::errc{} || parsed.ptr != encoded.data() + i + 3)
                    throw std::runtime_error("Invalid file URL escape");
                byte = static_cast<char>(value);
                i += 2;
            }
            if (!byte)
                throw std::runtime_error("File URL contains NUL");
            text += byte;
        }
        return text;
    };
    const auto authority = decoded(rest.substr(0, slash));
    auto path = decoded(rest.substr(slash));
#ifdef _WIN32
    if (!authority.empty() && authority != "localhost") {
        if (authority.find_first_of("/\\:@") != authority.npos)
            throw std::runtime_error("Invalid file URL authority");
        path = "//" + authority + path;
    } else if (path.size() >= 3 && path[0] == '/' && path[2] == ':' &&
               ((path[1] >= 'a' && path[1] <= 'z') || (path[1] >= 'A' && path[1] <= 'Z')))
        path.erase(0, 1);
#else
    if (!authority.empty() && authority != "localhost")
        throw std::runtime_error("Remote file URL authorities are unsupported on this host");
#endif
    const auto result = std::filesystem::u8path(path);
    if (!result.is_absolute())
        throw std::runtime_error("File URL needs an absolute path");
    return result;
}
} // namespace forge
