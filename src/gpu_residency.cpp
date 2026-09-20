#include "gpu_residency.hpp"
namespace forge::gpu_detail {
std::uint64_t Traits<MeshAsset>::bytes(const MeshResourceData& value) {
    validate_mesh(value.mesh);
    validate_mesh_material_bindings(value);
    std::uint64_t result = 0;
    for (const auto& lod : value.mesh.lods)
        for (const auto& part : lod.parts) {
            result += part.indices.size() * sizeof(std::uint32_t);
            for (const auto& stream : part.streams)
                result += stream.scalar_count() * 4;
            for (const auto& target : part.morph_targets)
                for (const auto& stream : target)
                    result += stream.scalar_count() * 4;
        }
    return result;
}
} // namespace forge::gpu_detail
