#include "gltf_native.hpp"
#include "gltf_validation.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <numeric>
#include <tiny_gltf.h>

namespace forge::asset_detail {
namespace {
using namespace gltf_detail;
enum class Semantic { Position, Normal, Tangent, Uv, Color, Joints, Weights, Custom };
std::pair<Semantic, unsigned> semantic(const std::string& name) {
    if (name.empty() || name.size() > 128)
        throw std::runtime_error("Invalid glTF attribute semantic length");
    if (name == "POSITION")
        return {Semantic::Position, 0};
    if (name == "NORMAL")
        return {Semantic::Normal, 0};
    if (name == "TANGENT")
        return {Semantic::Tangent, 0};
    if (name.front() == '_')
        return {Semantic::Custom, 0};
    for (const auto& [prefix, kind] : {std::pair{"TEXCOORD_", Semantic::Uv},
                                       {"COLOR_", Semantic::Color},
                                       {"JOINTS_", Semantic::Joints},
                                       {"WEIGHTS_", Semantic::Weights}}) {
        if (!name.starts_with(prefix))
            continue;
        const auto suffix = std::string_view(name).substr(std::char_traits<char>::length(prefix));
        unsigned index = 0;
        const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
        if (suffix.empty() || (suffix.size() > 1 && suffix[0] == '0') || parsed.ec != std::errc{} ||
            parsed.ptr != suffix.data() + suffix.size())
            throw std::runtime_error("Invalid glTF attribute set index: " + name);
        return {kind, index};
    }
    throw std::runtime_error(
        "Unknown glTF attribute requires application-specific underscore prefix: " + name);
}
void attribute_format(const tinygltf::Accessor& accessor, Semantic kind, bool morph,
                      bool quantization) {
    const bool fp = accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT;
    const bool unorm =
        accessor.normalized && (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                                accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    const bool snorm =
        accessor.normalized && (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE ||
                                accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT);
    const bool sint = accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE ||
                      accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT;
    const bool uint = accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                      accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    bool valid = false;
    switch (kind) {
    case Semantic::Position:
        valid = (fp || (quantization && (sint || (!morph && uint)))) &&
                accessor.type == TINYGLTF_TYPE_VEC3;
        break;
    case Semantic::Normal:
        valid = (fp || (quantization && snorm)) && accessor.type == TINYGLTF_TYPE_VEC3;
        break;
    case Semantic::Tangent:
        valid = (fp || (quantization && snorm)) &&
                accessor.type == (morph ? TINYGLTF_TYPE_VEC3 : TINYGLTF_TYPE_VEC4);
        break;
    case Semantic::Uv:
        valid = (fp || unorm || (morph && snorm) || (quantization && (sint || (!morph && uint)))) &&
                accessor.type == TINYGLTF_TYPE_VEC2;
        break;
    case Semantic::Color:
        valid = (fp || unorm || (morph && snorm)) &&
                (accessor.type == TINYGLTF_TYPE_VEC3 || accessor.type == TINYGLTF_TYPE_VEC4);
        break;
    case Semantic::Joints:
        valid = !morph && !accessor.normalized && accessor.type == TINYGLTF_TYPE_VEC4 &&
                (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                 accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
        break;
    case Semantic::Weights:
        valid = !morph && (fp || unorm) && accessor.type == TINYGLTF_TYPE_VEC4;
        break;
    case Semantic::Custom:
        valid = accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        break;
    }
    if (!valid)
        throw std::runtime_error("glTF attribute has an unsupported core shape/component format");
    if (kind == Semantic::Position &&
        (accessor.minValues.size() != 3 || accessor.maxValues.size() != 3))
        throw std::runtime_error("glTF POSITION requires min and max");
}
} // namespace
void validate_gltf_mesh_containers(const GltfSourceBundle& source) {
    const auto& meshes = array(source.document, "meshes", 1000000);
    std::size_t primitive_count = 0;
    for (const auto& mesh : meshes) {
        const auto& primitives = array(mesh, "primitives", 100000);
        if (primitives.empty() || primitives.size() > 1000000 - primitive_count)
            throw std::runtime_error("glTF mesh requires primitives within aggregate limit");
        primitive_count += primitives.size();
        const auto target_count = array(primitives[0], "targets", 256).size();
        for (const auto& primitive : primitives)
            if (array(primitive, "targets", 256).size() != target_count)
                throw std::runtime_error(
                    "glTF mesh primitives have inconsistent morph target counts");
        if (mesh.contains("weights")) {
            const auto& weights = array(mesh, "weights", 256);
            if (weights.size() != target_count)
                throw std::runtime_error("glTF mesh morph weight count is invalid");
            for (const auto& weight : weights)
                if (!weight.is_number() || !std::isfinite(weight.get<double>()))
                    throw std::runtime_error("glTF mesh morph weight must be finite");
        }
    }
}
NativeMeshPrimitive NativeGltfDocument::primitive(std::size_t mesh_index,
                                                  std::size_t primitive_index) const {
    const auto& meshes = meshes_;
    if (mesh_index >= meshes.size())
        throw std::runtime_error("glTF mesh index is invalid");
    const auto& primitives = array(meshes[mesh_index], "primitives", 100000);
    if (primitive_index >= primitives.size())
        throw std::runtime_error("glTF primitive index is invalid");
    const auto& primitive = primitives[primitive_index];
    const auto& attrs = primitive.at("attributes");
    if (!attrs.is_object() || attrs.empty() || attrs.size() > 64 || !attrs.contains("POSITION"))
        throw std::runtime_error("glTF primitive requires POSITION and at most 64 attributes");
    const auto& native = model();
    const auto& used = array(source_.document, "extensionsUsed", 256);
    const auto& required = array(source_.document, "extensionsRequired", 256);
    const bool quantization =
        std::find(used.begin(), used.end(), "KHR_mesh_quantization") != used.end();
    if (quantization &&
        std::find(required.begin(), required.end(), "KHR_mesh_quantization") == required.end())
        throw std::runtime_error("KHR_mesh_quantization must be a required extension");
    auto accessor = [&](const Json& index) -> const tinygltf::Accessor& {
        const auto value = size_value(index);
        if (value >= native.accessors.size())
            throw std::runtime_error("glTF attribute accessor index is invalid");
        return native.accessors[value];
    };
    NativeMeshPrimitive result;
    result.vertex_count = accessor(attrs.at("POSITION")).count;
    std::size_t bytes = 0;
    auto budget = [&](std::size_t count, std::size_t width) {
        constexpr std::size_t limit = 512 * 1024 * 1024;
        if (!width || count > (limit - bytes) / width)
            throw std::runtime_error("glTF primitive decoded data exceeds 512 MiB");
        bytes += count * width;
    };
    std::map<Semantic, std::set<unsigned>> sets;
    auto decode = [&](const std::string& name, const Json& index, bool morph) {
        const auto& input = accessor(index);
        const auto [kind, set] = semantic(name);
        attribute_format(input, kind, morph, quantization);
        if (input.count != result.vertex_count)
            throw std::runtime_error("glTF primitive/morph attribute counts disagree");
        if (!morph && kind != Semantic::Custom)
            sets[kind].insert(set);
        const auto components = tinygltf::GetNumComponentsInType(input.type);
        if (components <= 0)
            throw std::runtime_error("Invalid glTF attribute dimension");
        budget(input.count, static_cast<std::size_t>(components) * sizeof(float));
        if (input.bufferView >= 0) {
            const auto& view = native.bufferViews.at(static_cast<std::size_t>(input.bufferView));
            const auto size = tinygltf::GetComponentSizeInBytes(input.componentType);
            if (input.byteOffset % 4 || ((size * components) % 4 && !view.byteStride))
                throw std::runtime_error(
                    "glTF vertex attribute elements must be aligned to 4 bytes");
            if (view.target && view.target != TINYGLTF_TARGET_ARRAY_BUFFER)
                throw std::runtime_error("glTF vertex attribute uses an index buffer target");
        }
        return kind;
    };
    for (const auto& [name, index] : attrs.items()) {
        const auto kind = decode(name, index, false);
        if (kind == Semantic::Joints) {
            result.integer_attributes.emplace(name, unsigned_integers(size_value(index)));
            continue;
        }
        auto values = floats(size_value(index));
        if (kind == Semantic::Normal || kind == Semantic::Tangent) {
            for (std::size_t i = 0; i < values.count; ++i) {
                const auto offset = i * values.components;
                const double length =
                    std::hypot(double(values.values[offset]), double(values.values[offset + 1]),
                               double(values.values[offset + 2]));
                const bool quantized = accessor(index).componentType != 5126;
                if (!length || (!quantized && std::abs(length - 1.0) > 0.001))
                    throw std::runtime_error("glTF normal/tangent direction must be normalized");
                if (quantized)
                    for (unsigned axis = 0; axis < 3; ++axis)
                        values.values[offset + axis] = float(values.values[offset + axis] / length);
                if (kind == Semantic::Tangent && std::abs(values.values[offset + 3]) != 1.f)
                    throw std::runtime_error("glTF tangent handedness must be +1 or -1");
            }
        } else if (kind == Semantic::Color && name == "COLOR_0") {
            for (auto& value : values.values)
                value = std::clamp(value, 0.f, 1.f);
        } else if (kind == Semantic::Weights) {
            for (auto value : values.values)
                if (value < 0)
                    throw std::runtime_error("glTF skin weight must be nonnegative");
        } else if (kind == Semantic::Custom) {
            result.diagnostics.push_back("Preserved application-specific vertex attribute " + name);
        }
        result.attributes.emplace(name, std::move(values));
    }
    for (const auto kind : {Semantic::Uv, Semantic::Color, Semantic::Joints, Semantic::Weights}) {
        unsigned next = 0;
        for (auto set : sets[kind])
            if (set != next++)
                throw std::runtime_error(
                    "glTF attribute sets must start at zero and be consecutive");
    }
    if (sets[Semantic::Joints] != sets[Semantic::Weights])
        throw std::runtime_error("glTF joint and weight set counts disagree");
    // Quantized weights have an exact normative sum. Reconstruct their common
    // 65535 denominator (255 divides 65535) after native normalized decoding.
    bool quantized_only = !sets[Semantic::Weights].empty();
    for (auto set : sets[Semantic::Weights])
        quantized_only &=
            accessor(attrs.at("WEIGHTS_" + std::to_string(set))).componentType != 5126;
    if (quantized_only)
        for (std::size_t vertex = 0; vertex < result.vertex_count; ++vertex) {
            std::uint64_t sum = 0;
            for (auto set : sets[Semantic::Weights]) {
                const auto& weights = result.attributes.at("WEIGHTS_" + std::to_string(set)).values;
                for (unsigned k = 0; k < 4; ++k)
                    sum += static_cast<std::uint64_t>(
                        std::llround(double(weights[vertex * 4 + k]) * 65535));
            }
            if (sum != 65535)
                throw std::runtime_error("glTF quantized skin weights must sum exactly to one");
        }
    const auto& position = result.attributes.at("POSITION").values;
    std::copy_n(position.begin(), 3, result.minimum.begin());
    result.maximum = result.minimum;
    for (std::size_t i = 0; i < result.vertex_count; ++i)
        for (unsigned axis = 0; axis < 3; ++axis) {
            result.minimum[axis] = std::min(result.minimum[axis], position[i * 3 + axis]);
            result.maximum[axis] = std::max(result.maximum[axis], position[i * 3 + axis]);
        }
    const auto& source_bounds = accessor(attrs.at("POSITION"));
    const auto bound_value = [&](double value) {
        if (!source_bounds.normalized)
            return value;
        switch (source_bounds.componentType) {
        case 5120:
            return std::max(value / 127.0, -1.0);
        case 5121:
            return value / 255.0;
        case 5122:
            return std::max(value / 32767.0, -1.0);
        case 5123:
            return value / 65535.0;
        default:
            return value;
        }
    };
    bool bounds_changed = false;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto tolerance = 0.000001 * std::max({1.0, std::abs(double(result.minimum[axis])),
                                                    std::abs(double(result.maximum[axis]))});
        bounds_changed |=
            std::abs(bound_value(source_bounds.minValues[axis]) - result.minimum[axis]) >
                tolerance ||
            std::abs(bound_value(source_bounds.maxValues[axis]) - result.maximum[axis]) > tolerance;
    }
    if (bounds_changed)
        result.diagnostics.push_back(
            "Recomputed POSITION bounds differ from declared source bounds");
    if (primitive.contains("material")) {
        const auto material = size_value(primitive.at("material"));
        if (material >= native.materials.size())
            throw std::runtime_error("glTF primitive material index is invalid");
        result.material = static_cast<int>(material);
    }
    std::vector<std::uint32_t> indices;
    if (primitive.contains("indices")) {
        const auto& input = accessor(primitive.at("indices"));
        if (input.type != TINYGLTF_TYPE_SCALAR || input.normalized ||
            (input.componentType != 5121 && input.componentType != 5123 &&
             input.componentType != 5125))
            throw std::runtime_error("glTF indices require non-normalized unsigned SCALAR values");
        if (input.bufferView >= 0) {
            const auto& view = native.bufferViews.at(static_cast<std::size_t>(input.bufferView));
            if (view.byteStride ||
                (view.target && view.target != TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER))
                throw std::runtime_error("glTF index buffer cannot have vertex stride/target");
        }
        budget(input.count, sizeof(std::uint32_t));
        indices = unsigned_integers(size_value(primitive.at("indices"))).values;
        const auto restart = input.componentType == 5121   ? 255u
                             : input.componentType == 5123 ? 65535u
                                                           : UINT32_MAX;
        for (auto index : indices)
            if (index >= result.vertex_count || index == restart)
                throw std::runtime_error(
                    "glTF index exceeds vertex count or uses forbidden restart value");
    } else {
        budget(result.vertex_count, sizeof(std::uint32_t));
        indices.resize(result.vertex_count);
        std::iota(indices.begin(), indices.end(), 0u);
    }
    const auto mode = size_or(primitive, "mode", 4), count = indices.size();
    if (!count || mode > 6 || (mode == 1 && count % 2) || ((mode == 2 || mode == 3) && count < 2) ||
        (mode == 4 && count % 3) || ((mode == 5 || mode == 6) && count < 3))
        throw std::runtime_error("glTF primitive index count is invalid for topology");
    result.topology = mode == 0   ? NativePrimitiveTopology::Points
                      : mode <= 3 ? NativePrimitiveTopology::Lines
                                  : NativePrimitiveTopology::Triangles;
    if (mode == 0 || mode == 1 || mode == 4)
        result.indices = std::move(indices);
    else {
        const auto expanded = mode == 2 ? count * 2 : mode == 3 ? (count - 1) * 2 : (count - 2) * 3;
        budget(expanded, sizeof(std::uint32_t));
        result.indices.reserve(expanded);
        if (mode == 2 || mode == 3) {
            for (std::size_t i = 1; i < count; ++i)
                result.indices.insert(result.indices.end(), {indices[i - 1], indices[i]});
            if (mode == 2)
                result.indices.insert(result.indices.end(), {indices.back(), indices.front()});
        } else {
            for (std::size_t i = 2; i < count; ++i) {
                const auto a = mode == 6 ? indices[0] : indices[i - 2];
                const auto b = indices[i - 1], c = indices[i];
                if (mode == 5 && i % 2)
                    result.indices.insert(result.indices.end(), {b, a, c});
                else
                    result.indices.insert(result.indices.end(), {a, b, c});
            }
        }
    }
    const auto& targets = array(primitive, "targets", 256);
    for (const auto& target : targets) {
        if (!target.is_object() || target.empty() || target.size() > attrs.size())
            throw std::runtime_error("glTF morph target must reference existing base attributes");
        std::map<std::string, NativeGltfValues<float>> output;
        for (const auto& [name, index] : target.items()) {
            if (!attrs.contains(name))
                throw std::runtime_error("glTF morph attribute has no base attribute");
            (void)decode(name, index, true);
            output.emplace(name, floats(size_value(index)));
        }
        result.morph_targets.push_back(std::move(output));
    }
    return result;
}
} // namespace forge::asset_detail
