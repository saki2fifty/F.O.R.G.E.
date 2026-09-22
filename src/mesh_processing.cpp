#include "mesh_processing.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <meshoptimizer.h>
#include <numeric>

namespace forge::asset_detail {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void cancelled(std::stop_token cancel) {
    require(!cancel.stop_requested(), "Mesh preparation cancelled");
}
const std::vector<float>& floats(const MeshPart& part, std::string_view name) {
    const auto* stream = part.find(name);
    require(stream && std::holds_alternative<std::vector<float>>(stream->values),
            "Mesh preparation requires a floating-point stream");
    return std::get<std::vector<float>>(stream->values);
}
const MeshStream* find(const std::vector<MeshStream>& streams, std::string_view name) {
    const auto it = std::find_if(streams.begin(), streams.end(),
                                 [&](const auto& s) { return s.semantic == name; });
    return it == streams.end() ? nullptr : &*it;
}
void replace(std::vector<MeshStream>& streams, std::string name, unsigned components,
             std::vector<float> values) {
    std::erase_if(streams, [&](const auto& s) { return s.semantic == name; });
    streams.push_back({std::move(name), components, std::move(values)});
}
std::vector<const MeshStream*> all_streams(const MeshPart& part) {
    std::vector<const MeshStream*> streams;
    for (const auto& stream : part.streams)
        streams.push_back(&stream);
    for (const auto& target : part.morph_targets)
        for (const auto& stream : target)
            streams.push_back(&stream);
    return streams;
}
// Copy each referenced vertex coherently; no channel is assigned a different
// permutation. This also handles corner expansion before generating directions.
void select_vertices(MeshPart& part, const std::vector<unsigned>& selection) {
    auto select = [&](MeshStream& stream) {
        std::visit(
            [&](auto& input) {
                using Vector = std::decay_t<decltype(input)>;
                Vector output(selection.size() * stream.components);
                for (std::size_t i = 0; i < selection.size(); ++i)
                    std::copy_n(input.data() + std::size_t(selection[i]) * stream.components,
                                stream.components, output.data() + i * stream.components);
                input = std::move(output);
            },
            stream.values);
    };
    for (auto& stream : part.streams)
        select(stream);
    for (auto& target : part.morph_targets)
        for (auto& stream : target)
            select(stream);
    part.vertices = static_cast<std::uint32_t>(selection.size());
}
void remap(MeshPart& part, const std::vector<unsigned>& mapping, std::size_t count) {
    std::vector<unsigned> selection(count, UINT32_MAX);
    for (std::size_t i = 0; i < mapping.size(); ++i)
        if (mapping[i] != UINT32_MAX) {
            require(mapping[i] < count, "Native mesh remap exceeds prepared vertex count");
            selection[mapping[i]] = static_cast<unsigned>(i);
        }
    require(std::find(selection.begin(), selection.end(), UINT32_MAX) == selection.end(),
            "Native mesh remap left an uninitialized vertex");
    meshopt_remapIndexBuffer(part.indices.data(), part.indices.data(), part.indices.size(),
                             mapping.data());
    select_vertices(part, selection);
}
std::vector<float> target_values(const MeshPart& part, const std::vector<MeshStream>& target,
                                 std::string_view name) {
    auto result = floats(part, name);
    if (const auto* delta = find(target, name)) {
        const auto& values = std::get<std::vector<float>>(delta->values);
        require(values.size() == result.size(), "Morph preparation stream shape differs");
        for (std::size_t i = 0; i < result.size(); ++i) {
            const double value = double(result[i]) + values[i];
            require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
                    "Morphed value cannot be represented by the mesh float profile");
            result[i] = static_cast<float>(value);
        }
    }
    return result;
}
std::vector<float> flat_normals(const std::vector<float>& positions, std::size_t& degenerate,
                                std::stop_token cancel) {
    std::vector<float> normals(positions.size());
    for (std::size_t face = 0; face < positions.size(); face += 9) {
        if (face % (9 * 4096) == 0)
            cancelled(cancel);
        double a[3], b[3], n[3];
        for (unsigned axis = 0; axis < 3; ++axis) {
            a[axis] = double(positions[face + 3 + axis]) - positions[face + axis];
            b[axis] = double(positions[face + 6 + axis]) - positions[face + axis];
        }
        for (unsigned axis = 0; axis < 3; ++axis)
            n[axis] = a[(axis + 1) % 3] * b[(axis + 2) % 3] - a[(axis + 2) % 3] * b[(axis + 1) % 3];
        const auto length = std::hypot(n[0], n[1], n[2]);
        if (!length)
            ++degenerate;
        for (unsigned corner = 0; corner < 3; ++corner)
            for (unsigned axis = 0; axis < 3; ++axis)
                normals[face + corner * 3 + axis] =
                    length ? float(n[axis] / length) : (axis == 1 ? 1.f : 0.f);
    }
    return normals;
}
void normalize_normals(std::vector<float>& normals, const std::vector<float>& fallback,
                       std::size_t& zero_count) {
    for (std::size_t i = 0; i < normals.size(); i += 3) {
        const auto length =
            std::hypot(double(normals[i]), double(normals[i + 1]), double(normals[i + 2]));
        if (!length) {
            std::copy_n(fallback.data() + i, 3, normals.data() + i);
            ++zero_count;
        } else {
            for (unsigned a = 0; a < 3; ++a)
                normals[i + a] = float(normals[i + a] / length);
        }
    }
}
// Tangent direction is invariant under a positive uniform position/UV scale.
// A power-of-two scale preserves source float bits while avoiding intermediate
// overflow/underflow; reject a dynamic range that would lose input information.
std::vector<float> conditioned(const std::vector<float>& source) {
    double magnitude = 0;
    for (float value : source)
        magnitude = std::max(magnitude, std::abs(double(value)));
    int exponent = 0;
    (void)std::frexp(magnitude, &exponent);
    std::vector<float> result(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        result[i] = static_cast<float>(std::ldexp(double(source[i]), -exponent));
        require(std::ldexp(double(result[i]), exponent) == double(source[i]),
                "Tangent geometry/UV dynamic range exceeds native float precision");
    }
    return result;
}
std::vector<float> tangents(const std::vector<float>& positions, const std::vector<float>& normals,
                            const std::vector<float>& uv) {
    const auto count = positions.size() / 3;
    std::vector<float> result(count * 4);
    const auto native_positions = conditioned(positions);
    const auto native_uv = conditioned(uv);
    meshopt_generateTangents(result.data(), nullptr, count, native_positions.data(), count, 12,
                             normals.data(), 12, native_uv.data(), 8, meshopt_TangentCompatible);
    for (std::size_t i = 0; i < count; ++i) {
        const auto length =
            std::hypot(double(result[i * 4]), double(result[i * 4 + 1]), double(result[i * 4 + 2]));
        require(std::isfinite(length) && std::abs(length - 1) <= .001 &&
                    std::abs(result[i * 4 + 3]) == 1,
                "Native tangent result cannot be represented by the mesh direction profile");
    }
    return result;
}
void directions(MeshPart& part, const MeshProcessingOptions& options, MeshLimits limits,
                std::vector<std::string>& diagnostics, std::stop_token cancel) {
    if (part.topology != MeshTopology::Triangles)
        return;
    const bool normals =
        options.normals == MeshDirections::Recalculate ||
        (options.normals == MeshDirections::GenerateMissing && !part.find("NORMAL"));
    const auto uv_it = options.tangent_uv_sets.find(part.material_slot);
    const auto uv =
        "TEXCOORD_" + std::to_string(uv_it == options.tangent_uv_sets.end() ? 0 : uv_it->second);
    const bool wants_tangents =
        options.tangents == MeshDirections::Recalculate ||
        (options.tangents == MeshDirections::GenerateMissing && (!part.find("TANGENT") || normals));
    const bool generate_tangents =
        wants_tangents && (normals || part.find("NORMAL")) && part.find(uv);
    if (wants_tangents && !generate_tangents &&
        (options.tangents == MeshDirections::Recalculate || uv_it != options.tangent_uv_sets.end()))
        throw std::runtime_error("Requested tangent generation requires normals and selected " +
                                 uv);
    if (!normals && !generate_tangents)
        return;
    std::size_t width = 0;
    for (const auto* stream : all_streams(part))
        width += stream->components * 4;
    // Reserve generated base/delta directions as well as original channels.
    const auto extra = (normals ? 12u : 0u) + (generate_tangents ? 16u : 0u);
    width += extra * (part.morph_targets.size() + 1);
    require(part.indices.size() <= limits.vertices && part.indices.size() <= limits.bytes / width,
            "Mesh corner preparation exceeds vertex/byte budget");
    const auto selection = part.indices;
    select_vertices(part, selection);
    std::iota(part.indices.begin(), part.indices.end(), 0u);
    std::size_t degenerate = 0, zero_normals = 0, changed_signs = 0;
    if (normals) {
        // Source tangents are not valid for newly generated flat normals.
        std::erase_if(part.streams, [](const auto& s) { return s.semantic == "TANGENT"; });
        for (auto& target : part.morph_targets)
            std::erase_if(target, [](const auto& s) { return s.semantic == "TANGENT"; });
        replace(part.streams, "NORMAL", 3,
                flat_normals(floats(part, "POSITION"), degenerate, cancel));
        for (auto& target : part.morph_targets) {
            auto values = flat_normals(target_values(part, target, "POSITION"), degenerate, cancel);
            const auto& base = floats(part, "NORMAL");
            for (std::size_t i = 0; i < values.size(); ++i)
                values[i] -= base[i];
            replace(target, "NORMAL", 3, std::move(values));
        }
    }
    if (generate_tangents) {
        replace(part.streams, "TANGENT", 4,
                tangents(floats(part, "POSITION"), floats(part, "NORMAL"), floats(part, uv)));
        const auto& base = floats(part, "TANGENT");
        for (auto& target : part.morph_targets) {
            cancelled(cancel);
            auto normal = target_values(part, target, "NORMAL");
            normalize_normals(normal, floats(part, "NORMAL"), zero_normals);
            const auto generated = tangents(target_values(part, target, "POSITION"), normal,
                                            target_values(part, target, uv));
            std::vector<float> delta(std::size_t(part.vertices) * 3);
            for (std::size_t i = 0; i < part.vertices; ++i) {
                changed_signs += generated[i * 4 + 3] != base[i * 4 + 3];
                for (unsigned axis = 0; axis < 3; ++axis)
                    delta[i * 3 + axis] = generated[i * 4 + axis] - base[i * 4 + axis];
            }
            replace(target, "TANGENT", 3, std::move(delta));
        }
    }
    if (degenerate)
        diagnostics.push_back(std::to_string(degenerate) +
                              " collapsed base/target triangles use a finite +Y normal fallback");
    if (zero_normals)
        diagnostics.push_back(std::to_string(zero_normals) +
                              " zero morph normals use the base direction for tangent preparation");
    if (changed_signs)
        diagnostics.push_back(
            std::to_string(changed_signs) +
            " morph corners change tangent orientation; glTF preserves base tangent W");
}
} // namespace
ProcessedMesh process_mesh(const MeshData& source, const MeshProcessingOptions& options,
                           MeshLimits limits, std::stop_token cancel) {
    cancelled(cancel);
    validate_mesh(source, limits);
    for (auto slot : options.order_independent_material_slots)
        require(slot < source.material_slots, "Mesh optimization material slot is invalid");
    for (const auto& [slot, uv] : options.tangent_uv_sets)
        require(slot < source.material_slots && uv <= 63,
                "Mesh tangent material/UV selection is invalid");
    ProcessedMesh result{source, {}};
    // Native offline passes need explicit connectivity. Preserve-only recipes
    // retain nonindexed input. Bound the aggregate extra working storage before
    // materializing equivalent lists; do not rescan the whole mesh per part.
    const bool connectivity = options.normals != MeshDirections::Preserve ||
                              options.tangents != MeshDirections::Preserve || options.weld_exact ||
                              options.optimize_vertex_fetch ||
                              !options.order_independent_material_slots.empty();
    if (connectivity) {
        auto bytes = source.byte_size();
        std::size_t indices = 0;
        for (const auto& lod : source.lods)
            for (const auto& part : lod.parts) {
                const auto count = part.indices.empty() ? part.vertices : part.indices.size();
                require(count <= limits.indices && indices <= limits.indices - count,
                        "Nonindexed preparation exceeds index budget");
                indices += count;
                if (part.indices.empty()) {
                    require(count <= limits.bytes / 4 && bytes <= limits.bytes - count * 4,
                            "Nonindexed preparation exceeds byte budget");
                    bytes += count * 4;
                }
            }
        for (auto& lod : result.mesh.lods)
            for (auto& part : lod.parts)
                if (part.indices.empty()) {
                    cancelled(cancel);
                    part.indices.resize(part.vertices);
                    std::iota(part.indices.begin(), part.indices.end(), 0u);
                }
    }
    for (auto& lod : result.mesh.lods)
        for (auto& part : lod.parts) {
            cancelled(cancel);
            directions(part, options, limits, result.diagnostics, cancel);
            if (options.weld_exact) {
                auto streams = all_streams(part);
                auto equal = [](void* context, unsigned a, unsigned b) {
                    const auto& streams =
                        *static_cast<const std::vector<const MeshStream*>*>(context);
                    for (const auto* stream : streams) {
                        const auto same = std::visit(
                            [&](const auto& values) {
                                const auto width = std::size_t(stream->components) * 4;
                                return std::memcmp(
                                           values.data() + std::size_t(a) * stream->components,
                                           values.data() + std::size_t(b) * stream->components,
                                           width) == 0;
                            },
                            stream->values);
                        if (!same)
                            return 0;
                    }
                    return 1;
                };
                std::vector<unsigned> mapping(part.vertices);
                const auto count = meshopt_generateVertexRemapCustom(
                    mapping.data(), part.indices.data(), part.indices.size(),
                    floats(part, "POSITION").data(), part.vertices, 12, equal, &streams);
                remap(part, mapping, count);
            }
            if (part.topology == MeshTopology::Triangles &&
                std::find(options.order_independent_material_slots.begin(),
                          options.order_independent_material_slots.end(),
                          part.material_slot) != options.order_independent_material_slots.end())
                meshopt_optimizeVertexCache(part.indices.data(), part.indices.data(),
                                            part.indices.size(), part.vertices);
            if (options.optimize_vertex_fetch) {
                std::vector<unsigned> mapping(part.vertices);
                const auto count = meshopt_optimizeVertexFetchRemap(
                    mapping.data(), part.indices.data(), part.indices.size(), part.vertices);
                remap(part, mapping, count);
            }
            part.bounds = mesh_bounds(part);
        }
    validate_mesh(result.mesh, limits);
    cancelled(cancel);
    return result;
}
} // namespace forge::asset_detail
