#include "gltf_draco.hpp"
#include "gltf_validation.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <draco/compression/decode.h>
#include <draco/mesh/mesh.h>
#include <map>

namespace forge::asset_detail {
namespace {
using namespace gltf_detail;
using Bytes = std::vector<std::byte>;
constexpr auto extension_name = "KHR_draco_mesh_compression";
struct Format {
    draco::DataType type;
    std::size_t width;
};
Format format(std::size_t type) {
    switch (type) {
    case 5120:
        return {draco::DT_INT8, 1};
    case 5121:
        return {draco::DT_UINT8, 1};
    case 5122:
        return {draco::DT_INT16, 2};
    case 5123:
        return {draco::DT_UINT16, 2};
    case 5125:
        return {draco::DT_UINT32, 4};
    case 5126:
        return {draco::DT_FLOAT32, 4};
    default:
        throw std::runtime_error("glTF Draco component type is invalid");
    }
}
std::size_t components(const Json& accessor) {
    const auto shape = accessor.at("type").get<std::string>();
    if (shape == "SCALAR")
        return 1;
    if (shape == "VEC2")
        return 2;
    if (shape == "VEC3")
        return 3;
    if (shape == "VEC4")
        return 4;
    throw std::runtime_error("glTF Draco attribute must be scalar or vector");
}
} // namespace
GltfSourceBundle decode_gltf_draco(const GltfSourceBundle& captured, std::size_t decoded_limit,
                                   std::stop_token cancel) {
    if (!decoded_limit || decoded_limit > 512 * 1024 * 1024)
        throw std::runtime_error("Invalid glTF Draco decoded-byte limit");
    const auto& source = captured.document;
    const auto& used = array(source, "extensionsUsed", 256);
    const auto& required = array(source, "extensionsRequired", 256);
    const bool declared = std::find(used.begin(), used.end(), extension_name) != used.end();
    const bool mandatory =
        std::find(required.begin(), required.end(), extension_name) != required.end();
    if (!declared && mandatory)
        throw std::runtime_error("glTF Draco extension is required but undeclared");
    const auto& meshes = array(source, "meshes", 1000000);
    const auto& views = array(source, "bufferViews", 1000000);
    const auto& accessors = array(source, "accessors", 1000000);
    auto result = captured;
    std::size_t total = 0;
    struct Published {
        Json accessor;
        std::shared_ptr<Bytes> bytes;
    };
    std::map<std::size_t, Published> outputs;
    auto reserve = [&](std::size_t count, std::size_t stride) {
        if (!count || !stride || count > (decoded_limit - total) / stride)
            throw std::runtime_error("glTF Draco decoded data exceeds byte limit");
        total += count * stride;
        return std::make_shared<Bytes>(count * stride);
    };
    auto publish = [&](std::size_t index, Json accessor, std::shared_ptr<Bytes> bytes,
                       std::size_t stride, bool vertex) {
        accessor.erase("bufferView");
        accessor.erase("byteOffset");
        // Sparse patches remain authored patches over the decoded attribute.
        if (const auto old = outputs.find(index); old != outputs.end()) {
            if (old->second.accessor != accessor || *old->second.bytes != *bytes)
                throw std::runtime_error("glTF Draco shared accessor has conflicting decoded data");
            return;
        }
        outputs.emplace(index, Published{accessor, bytes});
        const auto buffer = result.buffers.size();
        const auto view = result.document["bufferViews"].size();
        result.document["buffers"].push_back({{"byteLength", bytes->size()}});
        Json view_json{
            {"buffer", buffer}, {"byteLength", bytes->size()}, {"target", vertex ? 34962 : 34963}};
        if (vertex)
            view_json["byteStride"] = stride;
        result.document["bufferViews"].push_back(std::move(view_json));
        result.buffers.push_back({bytes, 0, bytes->size()});
        accessor["bufferView"] = view;
        accessor["byteOffset"] = 0;
        result.document["accessors"][index] = std::move(accessor);
    };
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        const auto& primitives = array(meshes[m], "primitives", 100000);
        for (std::size_t p = 0; p < primitives.size(); ++p) {
            if (cancel.stop_requested())
                throw std::runtime_error("glTF Draco decoding cancelled");
            const auto& primitive = primitives[p];
            const auto* ext = extension(primitive, extension_name);
            if (!ext)
                continue;
            if (!declared)
                throw std::runtime_error("glTF Draco extension is undeclared");
            const auto mode = size_or(primitive, "mode", 4);
            if (mode != 4 && mode != 5)
                throw std::runtime_error("glTF Draco requires triangle topology");
            const auto view_index = size_value(ext->at("bufferView"));
            if (view_index >= views.size())
                throw std::runtime_error("glTF Draco compressed view is invalid");
            const auto& view = views[view_index];
            const auto buffer = size_value(view.at("buffer"));
            const auto offset = size_or(view, "byteOffset", 0),
                       length = size_value(view.at("byteLength"));
            if (buffer >= captured.buffers.size() || !length ||
                offset > captured.buffers[buffer].length ||
                length > captured.buffers[buffer].length - offset || view.contains("byteStride"))
                throw std::runtime_error("glTF Draco compressed range is invalid");
            const auto& mapping = ext->at("attributes");
            const auto& attributes = primitive.at("attributes");
            if (!mapping.is_object() || mapping.empty() || mapping.size() > 64 ||
                !attributes.is_object())
                throw std::runtime_error("glTF Draco attribute map is invalid");
            // Admission of declared sizes precedes native decoding. The disposable
            // worker additionally bounds memory used by the native bitstream parser.
            for (const auto& [name, id] : mapping.items()) {
                if (!attributes.contains(name) || size_value(id) > UINT32_MAX)
                    throw std::runtime_error("glTF Draco attribute map is not a primitive subset");
                const auto index = size_value(attributes.at(name));
                if (index >= accessors.size())
                    throw std::runtime_error("glTF Draco accessor index is invalid");
                const auto& accessor = accessors[index];
                if (!mandatory && !accessor.contains("bufferView"))
                    throw std::runtime_error("glTF Draco without fallback must be required");
                const auto count = size_value(accessor.at("count"));
                const auto stride =
                    (components(accessor) * format(size_value(accessor.at("componentType"))).width +
                     3) &
                    ~std::size_t(3);
                if (!count || count > 16 * 1024 * 1024 || count > (decoded_limit - total) / stride)
                    throw std::runtime_error(
                        "glTF Draco declared attribute exceeds byte/vertex limit");
            }
            const auto compressed = captured.buffers[buffer].bytes().subspan(offset, length);
            draco::DecoderBuffer input;
            input.Init(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            draco::Decoder decoder;
            auto native = decoder.DecodeMeshFromBuffer(&input);
            if (!native.ok())
                throw std::runtime_error("glTF Draco native decoder rejected data");
            // The exact upstream glTF writer includes up to three zero bytes
            // from PadBuffer() in the compressed view's byteLength.
            const auto remaining = std::size_t(input.remaining_size());
            if (remaining > 3 || (remaining && (length % 4 != 0 ||
                                                !std::all_of(compressed.end() - remaining,
                                                             compressed.end(), [](std::byte value) {
                                                                 return value == std::byte{0};
                                                             }))))
                throw std::runtime_error("glTF Draco trailing compressed data");
            const auto& mesh = *native.value();
            const auto points = std::size_t(mesh.num_points()),
                       faces = std::size_t(mesh.num_faces());
            if (!points || points > 16 * 1024 * 1024 || !faces || faces > 48 * 1024 * 1024 / 3)
                throw std::runtime_error("glTF Draco decoded geometry exceeds limits");
            for (const auto& [name, id] : mapping.items()) {
                const auto index = size_value(attributes.at(name));
                const auto& accessor = accessors[index];
                const auto dims = components(accessor);
                const auto fmt = format(size_value(accessor.at("componentType")));
                const auto* attr =
                    mesh.GetAttributeByUniqueId(static_cast<std::uint32_t>(size_value(id)));
                if (!attr || std::size_t(attr->num_components()) != dims ||
                    attr->data_type() != fmt.type ||
                    attr->normalized() != accessor.value("normalized", false) ||
                    size_value(accessor.at("count")) != points)
                    throw std::runtime_error("glTF Draco decoded attribute differs from accessor");
                const auto width = dims * fmt.width, stride = (width + 3) & ~std::size_t(3);
                if (!attr->buffer() || attr->byte_stride() < std::int64_t(width) ||
                    attr->byte_offset() < 0 ||
                    (!attr->is_mapping_identity() && attr->indices_map_size() != points))
                    throw std::runtime_error("glTF Draco attribute storage/mapping is invalid");
                auto bytes = reserve(points, stride);
                for (std::size_t point = 0; point < points; ++point) {
                    if (point % 4096 == 0 && cancel.stop_requested())
                        throw std::runtime_error("glTF Draco decoding cancelled");
                    const auto entry = std::size_t(
                        attr->mapped_index(draco::PointIndex(static_cast<std::uint32_t>(point)))
                            .value());
                    const auto size = attr->buffer()->data_size(),
                               base = std::size_t(attr->byte_offset()),
                               step = std::size_t(attr->byte_stride());
                    if (entry >= attr->size() || base > size || width > size - base ||
                        entry > (size - base - width) / step)
                        throw std::runtime_error("glTF Draco attribute mapping exceeds storage");
                    const auto* from = attr->buffer()->data() + base + entry * step;
                    // Native values are host-endian; glTF transport is little-endian.
                    for (std::size_t c = 0; c < dims; ++c)
                        for (std::size_t b = 0; b < fmt.width; ++b)
                            (*bytes)[point * stride + c * fmt.width + b] = std::byte(
                                from[c * fmt.width + (std::endian::native == std::endian::little
                                                          ? b
                                                          : fmt.width - 1 - b)]);
                }
                publish(index, accessor, std::move(bytes), stride, true);
            }
            std::size_t index = result.document["accessors"].size();
            Json accessor{{"componentType", 5125}, {"type", "SCALAR"}, {"count", faces * 3}};
            if (primitive.contains("indices")) {
                index = size_value(primitive.at("indices"));
                if (index >= accessors.size())
                    throw std::runtime_error("glTF Draco index accessor is invalid");
                accessor = accessors[index];
                const auto type = size_value(accessor.at("componentType"));
                const auto count = size_value(accessor.at("count"));
                if (accessor.at("type") != "SCALAR" || accessor.value("normalized", false) ||
                    (type != 5121 && type != 5123 && type != 5125) ||
                    (mode == 4 && count != faces * 3) ||
                    (mode == 5 && (count < 3 || faces > count - 2)))
                    throw std::runtime_error("glTF Draco index accessor differs from geometry");
                if (mode == 5 && accessor.contains("sparse"))
                    throw std::runtime_error(
                        "glTF Draco sparse strip indices cannot be mapped to decoded faces");
                accessor["count"] = faces * 3;
            }
            const auto width = format(size_value(accessor.at("componentType"))).width;
            auto bytes = reserve(faces * 3, width);
            const auto forbidden = width == 1 ? 255u : width == 2 ? 65535u : UINT32_MAX;
            for (std::size_t f = 0; f < faces; ++f) {
                if (f % 4096 == 0 && cancel.stop_requested())
                    throw std::runtime_error("glTF Draco decoding cancelled");
                const auto face = mesh.face(draco::FaceIndex(static_cast<std::uint32_t>(f)));
                for (std::size_t c = 0; c < 3; ++c) {
                    const auto value = face[c].value();
                    if (value >= points || value >= forbidden)
                        throw std::runtime_error("glTF Draco face index exceeds accessor range");
                    for (std::size_t b = 0; b < width; ++b)
                        (*bytes)[(f * 3 + c) * width + b] = std::byte((value >> (b * 8)) & 255u);
                }
            }
            publish(index, std::move(accessor), std::move(bytes), width, false);
            auto& destination = result.document["meshes"][m]["primitives"][p];
            destination["indices"] = index;
            destination["mode"] = 4; // Native Draco connectivity is already a triangle list.
            destination["extensions"].erase(extension_name);
        }
    }
    return result;
}
} // namespace forge::asset_detail
