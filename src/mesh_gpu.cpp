#include "mesh_gpu.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>
namespace forge {
using namespace Diligent;
const GpuMeshAttribute* GpuMeshPart::find(std::string_view name) const {
    for (const auto& attribute : attributes)
        if (attribute.semantic == name)
            return &attribute;
    return nullptr;
}
const GpuMeshAttribute* GpuMorphTarget::find(std::string_view name) const {
    for (const auto& attribute : attributes)
        if (attribute.semantic == name)
            return &attribute;
    return nullptr;
}
GpuMesh upload_mesh(IRenderDevice* device, const MeshData& input) {
    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    if (!device)
        throw std::runtime_error("Mesh upload requires a render device");
    validate_mesh(input);
    GpuMesh candidate;
    auto buffer = [&](const void* data, std::size_t bytes, BIND_FLAGS bind, const char* name) {
        BufferDesc desc;
        desc.Name = name;
        desc.Size = bytes;
        desc.BindFlags = bind;
        desc.Usage = USAGE_IMMUTABLE;
        if (bind == BIND_SHADER_RESOURCE)
            desc.Mode = BUFFER_MODE_RAW;
        BufferData initial{data, bytes};
        RefCntAutoPtr<IBuffer> result;
        device->CreateBuffer(desc, &initial, &result);
        if (!result)
            throw std::runtime_error("Diligent mesh buffer allocation failed");
        candidate.buffer_bytes += bytes;
        return result;
    };
    for (const auto& input_lod : input.lods) {
        auto& lod = candidate.lods.emplace_back();
        lod.screen_coverage = input_lod.screen_coverage;
        for (const auto& source : input_lod.parts) {
            auto& part = lod.parts.emplace_back();
            part.vertex_count = source.vertices;
            part.index_count = static_cast<unsigned>(source.indices.size());
            part.material_slot = source.material_slot;
            part.bounds = source.bounds;
            part.joint_palette = source.joint_palette;
            switch (source.topology) {
            case MeshTopology::Points:
                part.topology = PRIMITIVE_TOPOLOGY_POINT_LIST;
                break;
            case MeshTopology::Lines:
                part.topology = PRIMITIVE_TOPOLOGY_LINE_LIST;
                break;
            case MeshTopology::Triangles:
                part.topology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                break;
            }
            for (const auto& stream : source.streams) {
                const auto type = std::holds_alternative<std::vector<float>>(stream.values)
                                      ? VT_FLOAT32
                                      : VT_UINT32;
                part.attributes.push_back({stream.semantic, stream.components, part.stride, type});
                part.stride += stream.components * sizeof(float);
            }
            std::vector<std::byte> packed(std::size_t(part.stride) * source.vertices);
            for (std::size_t i = 0; i < source.streams.size(); ++i) {
                const auto& stream = source.streams[i];
                const auto width = stream.components * sizeof(float);
                const auto* data = std::visit(
                    [](const auto& v) { return reinterpret_cast<const std::byte*>(v.data()); },
                    stream.values);
                for (std::size_t vertex = 0; vertex < source.vertices; ++vertex)
                    std::memcpy(packed.data() + vertex * part.stride + part.attributes[i].offset,
                                data + vertex * width, width);
            }
            part.vertices = buffer(packed.data(), packed.size(), BIND_VERTEX_BUFFER,
                                   "FORGE immutable cooked vertices");
            if (!source.indices.empty())
                part.indices = buffer(source.indices.data(), source.indices.size() * sizeof(Uint32),
                                      BIND_INDEX_BUFFER, "FORGE immutable cooked indices");
            std::vector<std::byte> deltas;
            for (const auto& target : source.morph_targets) {
                auto& offsets = part.morph_targets.emplace_back();
                for (const auto& stream : target) {
                    const auto offset = static_cast<unsigned>(deltas.size());
                    const auto type = std::holds_alternative<std::vector<float>>(stream.values)
                                          ? VT_FLOAT32
                                          : VT_UINT32;
                    offsets.attributes.push_back(
                        {stream.semantic, stream.components, offset, type});
                    std::visit(
                        [&](const auto& data) {
                            const auto bytes = data.size() * sizeof(data[0]);
                            deltas.resize(deltas.size() + bytes);
                            std::memcpy(deltas.data() + offset, data.data(), bytes);
                        },
                        stream.values);
                }
            }
            if (!deltas.empty())
                part.morphs = buffer(deltas.data(), deltas.size(), BIND_SHADER_RESOURCE,
                                     "FORGE immutable cooked morph deltas");
        }
    }
    return candidate;
}
} // namespace forge
