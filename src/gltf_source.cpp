#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "gltf_validation.hpp"
#include <algorithm>
#include <forge/gltf_source.hpp>
#include <limits>

namespace forge {
namespace {
using Json = nlohmann::json;
using namespace gltf_detail;
using Bytes = std::vector<std::byte>;
std::uint32_t u32(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw std::runtime_error("Truncated GLB integer");
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result |= std::uint32_t(std::to_integer<unsigned char>(bytes[offset + i])) << (i * 8);
    return result;
}
std::string text(const Json& value, std::size_t limit) {
    if (!value.is_string())
        throw std::runtime_error("glTF string has wrong type");
    const auto& result = value.get_ref<const std::string&>();
    if (result.empty() || result.size() > limit || result.find('\0') != std::string::npos)
        throw std::runtime_error("glTF string is empty or exceeds limit");
    return result;
}
int hex(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
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
            if (uri.size() - i < 3 || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0)
                throw std::runtime_error("Invalid percent encoding in glTF resource URI");
            c = static_cast<unsigned char>((hex(uri[i + 1]) << 4) | hex(uri[i + 2]));
            i += 2;
        }
        if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '?' || c == '#')
            throw std::runtime_error("Nonportable/unsafe glTF resource URI");
        decoded += static_cast<char>(c);
    }
    if (decoded.size() > 4096 || decoded.front() == '/')
        throw std::runtime_error("glTF resource URI exceeds bounds or names an absolute path");
    // The JSON library's UTF-8 validation rejects invalid percent-decoded names.
    (void)Json(decoded).dump();
    return decoded;
}
std::pair<std::string, Bytes> decode_data(std::string_view uri, std::size_t limit) {
    const auto comma = uri.find(',');
    if (!uri.starts_with("data:") || comma == std::string_view::npos || comma > 128)
        throw std::runtime_error("Invalid glTF data URI");
    auto header = uri.substr(5, comma - 5);
    const auto input = uri.substr(comma + 1);
    if (!header.ends_with(";base64")) {
        if (header.empty() || header.find(';') != std::string_view::npos || input.empty())
            throw std::runtime_error("Unsupported glTF data URI media parameters or empty data");
        Bytes output;
        for (std::size_t i = 0; i < input.size(); ++i) {
            auto value = static_cast<unsigned char>(input[i]);
            if (value == '%') {
                if (input.size() - i < 3 || hex(input[i + 1]) < 0 || hex(input[i + 2]) < 0)
                    throw std::runtime_error("Invalid percent encoding in glTF data URI");
                value = static_cast<unsigned char>((hex(input[i + 1]) << 4) | hex(input[i + 2]));
                i += 2;
            } else if (value < 33 || value > 126) {
                throw std::runtime_error("glTF binary data URI bytes must be percent encoded");
            }
            if (output.size() >= limit)
                throw std::runtime_error("glTF decoded data exceeds limit");
            output.push_back(std::byte(value));
        }
        return {std::string(header), std::move(output)};
    }
    header.remove_suffix(7);
    if (input.empty() || input.size() % 4 || input.size() / 4 > (limit + 2) / 3)
        throw std::runtime_error("glTF base64 data exceeds limits or has invalid length");
    auto digit = [](char c) {
        return c >= 'A' && c <= 'Z'   ? c - 'A'
               : c >= 'a' && c <= 'z' ? c - 'a' + 26
               : c >= '0' && c <= '9' ? c - '0' + 52
               : c == '+'             ? 62
               : c == '/'             ? 63
                                      : -1;
    };
    Bytes output;
    output.reserve(std::min(limit, input.size() / 4 * 3));
    for (std::size_t i = 0; i < input.size(); i += 4) {
        const int a = digit(input[i]), b = digit(input[i + 1]);
        const bool p2 = input[i + 2] == '=', p3 = input[i + 3] == '=';
        const int c = p2 ? 0 : digit(input[i + 2]), d = p3 ? 0 : digit(input[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (p2 && !p3) ||
            ((p2 || p3) && i + 4 != input.size()) || (p2 && (b & 15)) || (!p2 && p3 && (c & 3)))
            throw std::runtime_error("Invalid/noncanonical glTF base64 data");
        output.push_back(std::byte((a << 2) | (b >> 4)));
        if (!p2)
            output.push_back(std::byte(((b & 15) << 4) | (c >> 2)));
        if (!p3)
            output.push_back(std::byte(((c & 3) << 6) | d));
    }
    if (output.size() > limit)
        throw std::runtime_error("glTF decoded data exceeds limit");
    return {std::string(header), std::move(output)};
}
} // namespace
std::span<const std::byte> GltfByteRange::bytes() const {
    if (!storage || offset > storage->size() || length > storage->size() - offset)
        throw std::runtime_error("Invalid captured glTF byte range");
    return std::span<const std::byte>(*storage).subspan(offset, length);
}
GltfSourceBundle capture_gltf_source(const std::filesystem::path& project,
                                     const std::filesystem::path& source,
                                     const std::set<std::string>& supported_required_extensions,
                                     GltfSourceLimits limits, std::stop_token cancel) {
    if (!limits.file_bytes || limits.file_bytes > 1024ull * 1024 * 1024 ||
        limits.total_bytes < limits.file_bytes || limits.total_bytes > 2ull * 1024 * 1024 * 1024 ||
        !limits.json_bytes || limits.json_bytes > 64 * 1024 * 1024 || !limits.source_files ||
        limits.source_files > 4096 || !limits.array_entries || limits.array_entries > 1000000)
        throw std::runtime_error("Invalid glTF source admission limits");
    ProjectPaths paths(project);
    GltfSourceBundle result;
    result.source = ProjectPaths::normalize(source);
    struct Captured {
        GltfByteRange range;
        std::string digest;
    };
    std::map<std::filesystem::path, Captured, ProjectLocatorLess> captured;
    auto cancelled = [&] {
        if (cancel.stop_requested())
            throw std::runtime_error("glTF source capture cancelled");
    };
    auto account = [&](std::size_t count) {
        if (count > limits.total_bytes - result.captured_bytes)
            throw std::runtime_error("glTF captured sources exceed total byte limit");
        result.captured_bytes += count;
    };
    auto read = [&](const std::filesystem::path& locator, const std::string& role) {
        cancelled();
        const auto normalized = paths.relative(paths.resolve(locator));
        if (const auto found = captured.find(normalized); found != captured.end()) {
            if (role != "source")
                result.dependencies.push_back({normalized, role, found->second.digest});
            return found->second.range;
        }
        if (captured.size() >= limits.source_files)
            throw std::runtime_error("glTF source dependency file count exceeds limit");
        auto data = std::make_shared<Bytes>(asset_detail::read_bytes(
            paths.resolve(normalized),
            std::min(limits.file_bytes, limits.total_bytes - result.captured_bytes)));
        account(data->size());
        GltfByteRange range{data, 0, data->size()};
        const auto digest = asset_detail::content_digest(*data);
        captured.emplace(normalized, Captured{range, digest});
        if (role != "source")
            result.dependencies.push_back({normalized, role, digest});
        return range;
    };
    const auto container = read(result.source, "source");
    result.source_digest = asset_detail::content_digest(container.bytes());
    auto json = container.bytes();
    std::optional<GltfByteRange> bin;
    if (json.size() >= 4 && u32(json, 0) == 0x46546c67) {
        result.binary_container = true;
        if (json.size() < 20 || u32(json, 4) != 2 || u32(json, 8) != json.size())
            throw std::runtime_error("Invalid GLB version/declared length");
        const auto bytes = json;
        std::size_t offset = 12, chunk_index = 0;
        bool saw_json = false;
        while (offset < bytes.size()) {
            if (bytes.size() - offset < 8)
                throw std::runtime_error("Truncated GLB chunk header");
            const auto length = u32(bytes, offset), kind = u32(bytes, offset + 4);
            offset += 8;
            if (length % 4 || length > bytes.size() - offset)
                throw std::runtime_error("GLB chunk length/alignment exceeds container");
            if (kind == 0x4e4f534a) {
                if (saw_json || chunk_index != 0 || !length)
                    throw std::runtime_error("GLB JSON must be the first, unique, nonempty chunk");
                json = bytes.subspan(offset, length);
                saw_json = true;
            } else if (kind == 0x004e4942) {
                if (!saw_json || bin || chunk_index != 1)
                    throw std::runtime_error("GLB BIN must be the unique second chunk");
                bin = GltfByteRange{container.storage, offset, length};
            } else {
                if (!saw_json)
                    throw std::runtime_error("GLB must begin with JSON");
                if (result.diagnostics.size() < 128)
                    result.diagnostics.push_back("Ignored unknown optional GLB chunk type " +
                                                 std::to_string(kind));
            }
            offset += length;
            ++chunk_index;
            if (chunk_index > 4096)
                throw std::runtime_error("GLB chunk count exceeds limit");
        }
    }
    if (json.empty() || json.size() > limits.json_bytes)
        throw std::runtime_error("glTF JSON exceeds byte limit");
    result.document = asset_detail::parse_bounded_json(json, limits.json_bytes);
    const auto& document = result.document;
    if (!document.is_object() || document.at("asset").at("version") != "2.0" ||
        (document.at("asset").contains("minVersion") &&
         document.at("asset").at("minVersion") != "2.0"))
        throw std::runtime_error("Only glTF 2.0 source version is supported");
    std::set<std::string> used, required;
    for (const auto& extension : array(document, "extensionsUsed", 256))
        if (!used.insert(text(extension, 256)).second)
            throw std::runtime_error("Duplicate glTF extensionsUsed entry");
    for (const auto& extension : array(document, "extensionsRequired", 256)) {
        const auto name = text(extension, 256);
        if (!required.insert(name).second || !used.contains(name))
            throw std::runtime_error("glTF required extension is duplicated or not declared used");
        if (!supported_required_extensions.contains(name))
            throw std::runtime_error("Unsupported required glTF extension: " + name);
    }
    for (const auto& name : used)
        if (!required.contains(name))
            result.optional_extensions.push_back(name);
    for (const auto* field :
         {"accessors", "animations", "buffers", "bufferViews", "cameras", "images", "materials",
          "meshes", "nodes", "samplers", "scenes", "skins", "textures"})
        (void)array(document, field, limits.array_entries);

    auto resource = [&](const std::string& uri, const std::string& role, std::string& mime) {
        cancelled();
        if (uri.starts_with("data:")) {
            auto decoded = decode_data(
                uri, std::min(limits.file_bytes, limits.total_bytes - result.captured_bytes));
            mime = std::move(decoded.first);
            auto data = std::make_shared<Bytes>(std::move(decoded.second));
            account(data->size());
            return GltfByteRange{data, 0, data->size()};
        }
        const auto decoded = decode_path_uri(uri);
        return read(
            ProjectPaths::normalize(result.source.parent_path() / std::filesystem::u8path(decoded)),
            role);
    };
    const auto& buffers = array(document, "buffers", limits.array_entries);
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        cancelled();
        const auto& buffer = buffers[i];
        const auto length = size_value(buffer.at("byteLength"));
        if (!length || length > limits.file_bytes)
            throw std::runtime_error("glTF buffer byteLength exceeds limit or is zero");
        GltfByteRange bytes;
        if (buffer.contains("uri")) {
            std::string mime;
            bytes = resource(text(buffer.at("uri"), limits.json_bytes),
                             "gltf.buffer:" + std::to_string(i), mime);
            if (!mime.empty() && mime != "application/octet-stream" &&
                mime != "application/gltf-buffer")
                throw std::runtime_error("Invalid glTF buffer data URI media type");
        } else {
            if (i != 0 || !bin)
                throw std::runtime_error("glTF buffer has no admitted byte source");
            bytes = *bin;
            if (length > bytes.length || bytes.length - length > 3)
                throw std::runtime_error("GLB buffer length differs from BIN beyond padding");
            for (auto byte : bytes.bytes().subspan(length))
                if (byte != std::byte{0})
                    throw std::runtime_error("GLB BIN padding must be zero");
        }
        if (length > bytes.length)
            throw std::runtime_error("glTF buffer source is truncated");
        bytes.length = length;
        result.buffers.push_back(std::move(bytes));
    }
    const auto& views = array(document, "bufferViews", limits.array_entries);
    std::vector<GltfByteRange> ranges;
    for (const auto& view : views) {
        const auto index = size_value(view.at("buffer"));
        const auto offset = size_or(view, "byteOffset", 0),
                   length = size_value(view.at("byteLength"));
        if (index >= result.buffers.size() || !length || offset > result.buffers[index].length ||
            length > result.buffers[index].length - offset)
            throw std::runtime_error("glTF bufferView lies outside declared buffer");
        if (view.contains("byteStride")) {
            const auto stride = size_value(view.at("byteStride"));
            if (stride < 4 || stride > 252 || stride % 4)
                throw std::runtime_error("Invalid glTF bufferView byteStride");
        }
        if (view.contains("target")) {
            const auto target = size_value(view.at("target"));
            if (target != 34962 && target != 34963)
                throw std::runtime_error("Invalid glTF bufferView target");
        }
        auto range = result.buffers[index];
        range.offset += offset;
        range.length = length;
        ranges.push_back(std::move(range));
    }
    const auto& images = array(document, "images", limits.array_entries);
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto& item = images[i];
        if (item.contains("uri") == item.contains("bufferView"))
            throw std::runtime_error("glTF image requires exactly one URI or bufferView");
        GltfEncodedImage image;
        if (item.contains("mimeType"))
            image.mime_type = text(item.at("mimeType"), 128);
        if (item.contains("uri")) {
            std::string mime;
            image.encoded = resource(text(item.at("uri"), limits.json_bytes),
                                     "gltf.image:" + std::to_string(i), mime);
            if (!mime.empty() && !image.mime_type.empty() && mime != image.mime_type)
                throw std::runtime_error("glTF image data URI media type disagrees with mimeType");
            if (!mime.empty())
                image.mime_type = mime;
        } else {
            const auto index = size_value(item.at("bufferView"));
            if (index >= ranges.size() || image.mime_type.empty())
                throw std::runtime_error(
                    "glTF embedded image requires valid bufferView and mimeType");
            image.encoded = ranges[index];
        }
        if (!image.encoded.length)
            throw std::runtime_error("glTF encoded image is empty");
        result.images.push_back(std::move(image));
    }
    cancelled();
    return result;
}
} // namespace forge
