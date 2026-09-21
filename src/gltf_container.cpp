#include "gltf_container.hpp"
#include "bounded_json.hpp"
#include <limits>
#include <nlohmann/json.hpp>
namespace forge::gltf_detail {
namespace {
std::uint32_t u32(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw std::runtime_error("Truncated GLB integer");
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result |= std::uint32_t(std::to_integer<unsigned char>(bytes[offset + i])) << (i * 8);
    return result;
}
void put(std::string& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(char((value >> (8 * i)) & 255));
}
std::string encode_uri(std::string_view text) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
            result += char(c);
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
} // namespace
ContainerView gltf_container(std::span<const std::byte> bytes) {
    ContainerView result;
    result.json = bytes;
    if (bytes.size() < 4 || u32(bytes, 0) != 0x46546c67)
        return result;
    result.binary = true;
    if (bytes.size() < 20 || u32(bytes, 4) != 2 || u32(bytes, 8) != bytes.size())
        throw std::runtime_error("Invalid GLB version/declared length");
    bool saw_json = false;
    for (std::size_t offset = 12; offset < bytes.size();) {
        if (bytes.size() - offset < 8)
            throw std::runtime_error("Truncated GLB chunk header");
        const auto length = u32(bytes, offset), kind = u32(bytes, offset + 4);
        offset += 8;
        if (length % 4 || length > bytes.size() - offset)
            throw std::runtime_error("GLB chunk length/alignment exceeds container");
        const auto chunk = bytes.subspan(offset, length);
        if (kind == 0x4e4f534a) {
            if (saw_json || !result.chunks.empty() || !length)
                throw std::runtime_error("GLB JSON must be the first, unique, nonempty chunk");
            result.json = chunk;
            saw_json = true;
        } else if (kind == 0x004e4942) {
            if (!saw_json || result.bin || result.chunks.size() != 1)
                throw std::runtime_error("GLB BIN must be the unique second chunk");
            result.bin = chunk;
        } else if (!saw_json)
            throw std::runtime_error("GLB must begin with JSON");
        if (result.chunks.size() >= 4096)
            throw std::runtime_error("GLB chunk count exceeds limit");
        result.chunks.push_back({kind, chunk});
        offset += length;
    }
    return result;
}
std::string decode_path_uri(std::string_view uri) {
    if (uri.empty() || uri.size() > 12288 || uri.front() == '/' ||
        uri.find_first_of(":?#\\") != std::string_view::npos)
        throw std::runtime_error(
            "glTF resource must use a relative URI without scheme/query/fragment");
    std::string decoded;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        auto c = static_cast<unsigned char>(uri[i]);
        if (c == '%') {
            if (uri.size() - i < 3 || uri_hex(uri[i + 1]) < 0 || uri_hex(uri[i + 2]) < 0)
                throw std::runtime_error("Invalid percent encoding in glTF resource URI");
            c = static_cast<unsigned char>((uri_hex(uri[i + 1]) << 4) | uri_hex(uri[i + 2]));
            i += 2;
        }
        if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '?' || c == '#')
            throw std::runtime_error("Nonportable/unsafe glTF resource URI");
        decoded += static_cast<char>(c);
    }
    if (decoded.size() > 4096 || decoded.front() == '/')
        throw std::runtime_error("glTF resource URI exceeds bounds or names an absolute path");
    (void)nlohmann::json(decoded).dump();
    return decoded;
}
std::string relocate_gltf_source(const ProjectPaths& paths, const std::filesystem::path& source,
                                 const std::filesystem::path& destination,
                                 std::span<const std::byte> bytes) {
    constexpr std::size_t file_limit = 512ull * 1024 * 1024, json_limit = 64ull * 1024 * 1024;
    if (bytes.size() > file_limit)
        throw std::runtime_error("glTF relocation exceeds source byte budget");
    const auto from = ProjectPaths::normalize(source), to = ProjectPaths::normalize(destination);
    (void)paths.resolve(from);
    (void)paths.resolve(to);
    const auto container = gltf_container(bytes);
    auto document = asset_detail::parse_bounded_json(container.json, json_limit);
    if (!document.is_object() || document.at("asset").at("version") != "2.0")
        throw std::runtime_error("Relocation requires a glTF 2.0 source");
    bool changed = false;
    for (const char* field : {"buffers", "images"}) {
        const auto array = document.find(field);
        if (array == document.end())
            continue;
        if (!array->is_array() || array->size() > 100000)
            throw std::runtime_error("glTF relocation resource array exceeds profile");
        for (auto& item : *array) {
            if (!item.is_object())
                throw std::runtime_error("Invalid glTF relocation resource entry");
            const auto uri = item.find("uri");
            if (uri == item.end())
                continue;
            const auto original = uri->get<std::string>();
            if (original.starts_with("data:"))
                continue;
            const auto resource = ProjectPaths::normalize(
                from.parent_path() / std::filesystem::u8path(decode_path_uri(original)));
            (void)paths.resolve(resource);
            if (from.parent_path() == to.parent_path())
                continue;
            const auto base =
                to.parent_path().empty() ? std::filesystem::path(".") : to.parent_path();
            const auto relative = resource.lexically_relative(base);
            const auto next = encode_uri(path_utf8(relative));
            if (next.empty() ||
                ProjectPaths::normalize(to.parent_path() /
                                        std::filesystem::u8path(decode_path_uri(next))) != resource)
                throw std::runtime_error(
                    "glTF relocated URI does not resolve to its original resource");
            *uri = next;
            changed |= next != original;
        }
    }
    if (!changed)
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    auto json = document.dump();
    if (json.size() > json_limit)
        throw std::runtime_error("Relocated glTF JSON exceeds byte budget");
    if (!container.binary)
        return json;
    while (json.size() % 4)
        json += ' ';
    const auto total = bytes.size() - container.json.size() + json.size();
    if (total > file_limit || total > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Relocated GLB exceeds byte budget");
    std::string result;
    result.reserve(total);
    put(result, 0x46546c67);
    put(result, 2);
    put(result, std::uint32_t(total));
    for (const auto& chunk : container.chunks) {
        put(result, std::uint32_t(chunk.kind == 0x4e4f534a ? json.size() : chunk.bytes.size()));
        put(result, chunk.kind);
        if (chunk.kind == 0x4e4f534a)
            result += json;
        else
            result.append(reinterpret_cast<const char*>(chunk.bytes.data()), chunk.bytes.size());
    }
    return result;
}
} // namespace forge::gltf_detail
