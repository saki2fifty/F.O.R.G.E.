#include "asset_bytes.hpp"
#include "material_slot.hpp"
#include <forge/mesh_resource.hpp>
#include <set>
namespace forge {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
std::size_t MeshResourceData::resident_bytes() const {
    auto bytes = sizeof(*this) - sizeof(mesh) + mesh.resident_bytes() +
                 materials.capacity() * sizeof(MeshMaterialBinding);
    for (const auto& binding : materials)
        bytes += binding.key.capacity();
    return bytes;
}
void validate_mesh_material_bindings(const MeshResourceData& value) {
    std::set<std::uint32_t> used;
    for (const auto& lod : value.mesh.lods)
        for (const auto& part : lod.parts) {
            require(part.material_slot < value.mesh.material_slots,
                    "Mesh material slot out of range");
            used.insert(part.material_slot);
        }
    require(value.materials.size() == used.size(), "Mesh material binding coverage differs");
    std::set<std::string> keys;
    for (const auto& binding : value.materials) {
        detail::validate_material_slot_key(binding.key);
        require(used.erase(binding.physical_slot) == 1 && keys.insert(binding.key).second,
                "Duplicate or unused mesh material binding");
    }
}
MeshMaterialSelection select_mesh_materials(const MeshResourceData& mesh,
                                            std::span<const MaterialSlotOverride> overrides) {
    validate_mesh_material_bindings(mesh);
    detail::validate_material_slots(overrides);
    MeshMaterialSelection result{mesh.materials, {}};
    std::map<std::string, std::size_t> slots;
    for (std::size_t i = 0; i < result.bindings.size(); ++i)
        slots.emplace(result.bindings[i].key, i);
    for (const auto& override : overrides) {
        const auto found = slots.find(override.slot);
        if (found == slots.end())
            result.unresolved.push_back(override.slot);
        else
            result.bindings[found->second].material = override.material;
    }
    return result;
}

ResourcePool<MeshAsset>::Loader mesh_resource_loader(std::filesystem::path path, std::string digest,
                                                     MeshLimits limits,
                                                     std::vector<MeshMaterialBinding> bindings) {
    resource_detail::valid_revision(digest);
    if (limits.bytes > SIZE_MAX - 8 * 1024 * 1024 - 24)
        throw std::runtime_error("Mesh loader byte limit overflow");
    return [path = std::move(path), digest = std::move(digest), limits,
            bindings = std::move(bindings)](std::stop_token stop) {
        if (stop.stop_requested())
            throw std::runtime_error("Mesh resource load cancelled");
        auto bytes = asset_detail::read_bytes(path, limits.bytes + 8 * 1024 * 1024 + 24);
        if (asset_detail::content_digest(bytes) != digest)
            throw std::runtime_error("Cooked mesh content digest mismatch");
        if (stop.stop_requested())
            throw std::runtime_error("Mesh resource load cancelled");
        auto mesh = std::make_unique<MeshResourceData>();
        mesh->mesh = decode_mesh(bytes, limits);
        mesh->materials = bindings;
        // Identity-neutral single-default geometry needs no catalog assignment.
        // Other physical slots require a real logical binding from the caller.
        if (mesh->materials.empty() && mesh->mesh.material_slots == 1)
            mesh->materials.push_back({0, "default", {}});
        validate_mesh_material_bindings(*mesh);
        if (stop.stop_requested())
            throw std::runtime_error("Mesh resource load cancelled");
        const auto used = mesh->resident_bytes();
        return ResourceCandidate<MeshAsset>{std::move(mesh), ResourceMemory{used}};
    };
}
} // namespace forge
