#include "gltf_meshopt.hpp"
#include "gltf_validation.hpp"
#include <algorithm>
#include <bit>
#include <meshoptimizer.h>

namespace forge::asset_detail {
namespace {
using namespace gltf_detail;
using Bytes = std::vector<std::byte>;
// Filter inputs outside these normative EXT ranges have unspecified upstream
// behavior (including a zero-length octahedron). Reject before native filtering.
void validate_filter(std::span<const std::byte> bytes, std::size_t stride,
                     const std::string& filter, std::stop_token cancel) {
    const auto signed_component = [&](std::size_t offset, std::size_t width) {
        unsigned value = std::to_integer<unsigned>(bytes[offset]);
        if (width == 2)
            value |= std::to_integer<unsigned>(bytes[offset + 1]) << 8;
        const auto sign = 1u << (width * 8 - 1);
        return value & sign ? int(value) - int(sign * 2) : int(value);
    };
    if (filter == "NONE")
        return;
    for (std::size_t offset = 0; offset < bytes.size(); offset += stride) {
        if (offset % (stride * 4096) == 0 && cancel.stop_requested())
            throw std::runtime_error("glTF meshopt filtering cancelled");
        if (filter == "EXPONENTIAL") {
            for (std::size_t component = 0; component < stride; component += 4) {
                const auto exponent = signed_component(offset + component + 3, 1);
                if (exponent < -100 || exponent > 100)
                    throw std::runtime_error("glTF meshopt exponential filter exponent is invalid");
            }
            continue;
        }
        const auto width = stride / 4;
        const bool quaternion = filter == "QUATERNION";
        int one = signed_component(offset + (quaternion ? 3 : 2) * width, width);
        if (quaternion)
            one |= 3;
        if (one < (quaternion ? 7 : 1) || !std::has_single_bit(unsigned(one) + 1))
            throw std::runtime_error("glTF meshopt filter precision marker is invalid");
        for (std::size_t component = 0; component < (quaternion ? 3u : 2u); ++component) {
            const auto value = signed_component(offset + component * width, width);
            if (value < -one || value > one)
                throw std::runtime_error("glTF meshopt filter component exceeds encoded range");
        }
    }
}
} // namespace
GltfSourceBundle decode_gltf_meshopt(const GltfSourceBundle& captured, std::size_t decoded_limit,
                                     std::stop_token cancel) {
    if (!decoded_limit || decoded_limit > 512 * 1024 * 1024)
        throw std::runtime_error("Invalid glTF meshopt decoded-byte limit");
    const auto& original = captured.document;
    const auto& buffers = array(original, "buffers", 1000000);
    const auto& views = array(original, "bufferViews", 1000000);
    const auto& used = array(original, "extensionsUsed", 256);
    const auto& required = array(original, "extensionsRequired", 256);
    const bool declared = std::find(used.begin(), used.end(), meshopt_extension) != used.end();
    const bool mandatory =
        std::find(required.begin(), required.end(), meshopt_extension) != required.end();
    if (std::find(required.begin(), required.end(), "KHR_meshopt_compression") != required.end())
        throw std::runtime_error("KHR_meshopt_compression is not a ratified importer capability");
    if (buffers.size() != captured.buffers.size() || (mandatory && !declared))
        throw std::runtime_error("glTF meshopt source declarations are inconsistent");
    std::vector<bool> fallback(buffers.size());
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        if (const auto* ext = extension(buffers[i], meshopt_extension)) {
            if (!declared || extension(buffers[i], "KHR_meshopt_compression"))
                throw std::runtime_error(
                    "glTF meshopt buffer extension is undeclared or conflicting");
            if (ext->contains("fallback") && !ext->at("fallback").is_boolean())
                throw std::runtime_error("glTF meshopt fallback flag must be boolean");
            fallback[i] = ext->value("fallback", false);
        }
        if (!captured.buffers[i].storage && !mandatory)
            throw std::runtime_error("glTF meshopt placeholder requires the extension");
        if (size_value(buffers[i].at("byteLength")) != captured.buffers[i].length)
            throw std::runtime_error("glTF meshopt captured buffer size differs from declaration");
    }
    auto result = captured;
    std::size_t decoded = 0;
    for (std::size_t i = 0; i < views.size(); ++i) {
        if (cancel.stop_requested())
            throw std::runtime_error("glTF meshopt decoding cancelled");
        const auto& view = views[i];
        const auto parent = size_value(view.at("buffer"));
        const auto parent_offset = size_or(view, "byteOffset", 0);
        const auto length = size_value(view.at("byteLength"));
        if (parent >= buffers.size() || !length ||
            parent_offset > captured.buffers[parent].length ||
            length > captured.buffers[parent].length - parent_offset)
            throw std::runtime_error("glTF meshopt parent view exceeds buffer");
        const auto* ext = extension(view, meshopt_extension);
        if (!ext) {
            if (fallback[parent] || !captured.buffers[parent].storage)
                throw std::runtime_error("glTF meshopt fallback has an uncompressed view");
            continue;
        }
        if (!declared || extension(view, "KHR_meshopt_compression"))
            throw std::runtime_error("glTF meshopt view extension is undeclared or conflicting");
        const auto buffer = size_value(ext->at("buffer"));
        const auto begin = size_or(*ext, "byteOffset", 0);
        const auto size = size_value(ext->at("byteLength"));
        const auto stride = size_value(ext->at("byteStride"));
        const auto count = size_value(ext->at("count"));
        const auto mode = ext->at("mode").get<std::string>();
        const auto filter = ext->value("filter", std::string("NONE"));
        if (buffer >= buffers.size() || fallback[buffer] || !captured.buffers[buffer].storage ||
            !size || begin > captured.buffers[buffer].length ||
            size > captured.buffers[buffer].length - begin)
            throw std::runtime_error("glTF meshopt compressed range is invalid");
        if (!stride || stride > 256 || !count || count > (decoded_limit - decoded) / stride ||
            count * stride != length ||
            (view.contains("byteStride") && size_value(view.at("byteStride")) != stride))
            throw std::runtime_error(
                "glTF meshopt count/stride exceeds layout or decoded-byte limit");
        if ((mode != "ATTRIBUTES" && mode != "TRIANGLES" && mode != "INDICES") ||
            (mode == "ATTRIBUTES" && stride % 4) ||
            (mode != "ATTRIBUTES" && (stride != 2 && stride != 4)) ||
            (mode == "TRIANGLES" && count % 3))
            throw std::runtime_error("glTF meshopt compression mode/stride/count is invalid");
        if ((filter != "NONE" && filter != "OCTAHEDRAL" && filter != "QUATERNION" &&
             filter != "EXPONENTIAL") ||
            (mode != "ATTRIBUTES" && filter != "NONE") ||
            (filter == "OCTAHEDRAL" && stride != 4 && stride != 8) ||
            (filter == "QUATERNION" && stride != 8))
            throw std::runtime_error("glTF meshopt compression filter/stride is invalid");
        const auto input = captured.buffers[buffer].bytes().subspan(begin, size);
        const unsigned header = std::to_integer<unsigned>(input.front());
        const unsigned expected = mode == "ATTRIBUTES" ? 0xa0 : mode == "TRIANGLES" ? 0xe1 : 0xd1;
        if (header != expected)
            throw std::runtime_error("glTF meshopt bitstream version does not match EXT format");
        auto output = std::make_shared<Bytes>(length);
        const auto* data = reinterpret_cast<const unsigned char*>(input.data());
        const auto status =
            mode == "ATTRIBUTES"
                ? meshopt_decodeVertexBuffer(output->data(), count, stride, data, size)
            : mode == "TRIANGLES"
                ? meshopt_decodeIndexBuffer(output->data(), count, stride, data, size)
                : meshopt_decodeIndexSequence(output->data(), count, stride, data, size);
        if (status != 0)
            throw std::runtime_error("glTF meshopt native decoder rejected compressed data");
        validate_filter(*output, stride, filter, cancel);
        if (filter == "OCTAHEDRAL")
            meshopt_decodeFilterOct(output->data(), count, stride);
        else if (filter == "QUATERNION")
            meshopt_decodeFilterQuat(output->data(), count, stride);
        else if (filter == "EXPONENTIAL")
            meshopt_decodeFilterExp(output->data(), count, stride);
        auto& destination = result.document["bufferViews"][i];
        destination["buffer"] = result.buffers.size();
        destination["byteOffset"] = 0;
        destination["extensions"].erase(meshopt_extension);
        result.document["buffers"].push_back({{"byteLength", length}});
        result.buffers.push_back({std::move(output), 0, length});
        decoded += length;
    }
    // Unused placeholder buffers need only a legal transport buffer. No source
    // bufferView still references them; native parsing cannot allocate their size.
    for (std::size_t i = 0; i < buffers.size(); ++i)
        if (!result.buffers[i].storage) {
            auto empty = std::make_shared<Bytes>(1);
            result.buffers[i] = {std::move(empty), 0, 1};
            result.document["buffers"][i]["byteLength"] = 1;
        }
    const auto& images = array(original, "images", 1000000);
    if (images.size() != result.images.size())
        throw std::runtime_error("glTF meshopt image capture count is invalid");
    for (std::size_t i = 0; i < images.size(); ++i) {
        if (!images[i].contains("bufferView"))
            continue;
        const auto view_index = size_value(images[i].at("bufferView"));
        if (view_index >= views.size())
            throw std::runtime_error("glTF meshopt image view is invalid");
        const auto& view = result.document.at("bufferViews")[view_index];
        auto range = result.buffers.at(size_value(view.at("buffer")));
        range.offset += size_or(view, "byteOffset", 0);
        range.length = size_value(view.at("byteLength"));
        (void)range.bytes();
        result.images[i].encoded = std::move(range);
    }
    return result;
}
} // namespace forge::asset_detail
