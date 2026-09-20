#include "model_render_resource.hpp"
#include "pbr_material.hpp"
#include <forge/model_asset.hpp>
#include <forge/texture_bundle.hpp>
#include <set>
namespace forge::asset_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Selected {
    AssetId owner;
    std::string revision;
    std::uint64_t generation;
};
Selected selected_member(const AssetCatalog& catalog, AssetId id, std::string_view type) {
    const auto found = catalog.records().find(id);
    require(found != catalog.records().end() && found->second.type == type &&
                found->second.subasset && !found->second.subasset->removed,
            "Model render member is missing, removed or has the wrong type");
    const auto owner = found->second.subasset->owner;
    const auto root = catalog.records().find(owner);
    require(root != catalog.records().end() && root->second.type == ModelAsset::type &&
                !root->second.subasset,
            "Model render owner is missing or invalid");
    const auto& imported = root->second.metadata.at("forge.import");
    require(found->second.metadata.at("forge.import") == imported,
            "Model render member mixes publication revisions");
    const auto& generation = imported.at("generation");
    require(generation.is_number_unsigned() && generation.get<std::uint64_t>() > 0,
            "Invalid model render publication generation");
    return {owner, imported.at("key").get<std::string>(), generation.get<std::uint64_t>()};
}
ModelSelection load_selected(const std::filesystem::path& project, const AssetCatalog& catalog,
                             const Selected& selected, std::stop_token stop) {
    auto result = load_model_selection(project, catalog, selected.owner, stop);
    require(result.revision == selected.revision && result.generation == selected.generation,
            "Model render resource selection changed");
    return result;
}
} // namespace
MeshResourceData model_mesh_resource(const ModelSelection& selected, AssetRef<MeshAsset> mesh) {
    const auto& member = selected.member(mesh.id);
    require(member.identity.type == MeshAsset::type, "Selected resource is not a mesh");
    MeshResourceData result;
    result.mesh = decode_mesh(selected.bytes(member));
    std::set<std::uint32_t> used;
    for (const auto& lod : result.mesh.lods)
        for (const auto& part : lod.parts)
            used.insert(part.material_slot);
    for (const auto slot : used) {
        if (!slot) {
            result.materials.push_back({0, "default", {}});
            continue;
        }
        const auto material =
            selected.bindings.at(member.bindings.at("material." + std::to_string(slot)));
        require(selected.member(material).identity.type == MaterialAsset::type,
                "Mesh default material has the wrong type");
        // Asset identity survives source-array reorder and display-name changes.
        result.materials.push_back({slot, "material:" + material.str(), {material}});
    }
    validate_mesh_material_bindings(result);
    return result;
}
MaterialResourceData model_pbr_material_resource(const ModelSelection& selected,
                                                 AssetRef<MaterialAsset> material) {
    const auto& member = selected.member(material.id);
    require(member.identity.type == MaterialAsset::type, "Selected resource is not a material");
    MaterialResourceData result;
    result.values = decode_material(selected.bytes(member));
    (void)prepare_pbr_material(result.values);
    for (const auto& [role, address] : member.bindings) {
        const auto texture = selected.bindings.at(address);
        require(selected.member(texture).identity.type == TextureAsset::type,
                "Material texture has the wrong type");
        result.textures.emplace(role, AssetRef<TextureAsset>{texture});
    }
    validate_material_bindings(result.values, result.textures);
    return result;
}
MaterialResourceData model_material_resource(const ModelSelection& selected,
                                             AssetRef<MaterialAsset> material,
                                             const MaterialLayout& layout) {
    auto result = model_pbr_material_resource(selected, material);
    validate_material_layout(result.values, layout);
    return result;
}
TextureData model_texture_resource(const ModelSelection& selected, AssetRef<TextureAsset> texture,
                                   TextureSemantic semantic) {
    const auto& member = selected.member(texture.id);
    require(member.identity.type == TextureAsset::type, "Selected resource is not a texture");
    const auto index = decode_texture_bundle_index(selected.bytes(member));
    const auto& variant = index.find(semantic);
    for (const auto& file : selected.artifact->files) {
        if (file.name != variant.file)
            continue;
        auto result = decode_texture(file.bytes);
        require(result.semantic == semantic, "Model texture variant semantic mismatch");
        return result;
    }
    throw std::runtime_error("Selected model texture variant is missing");
}
ResourceTicket request_model_texture(ResourcePool<TextureAsset>& pool,
                                     std::filesystem::path project,
                                     std::shared_ptr<const AssetCatalog> catalog,
                                     AssetRef<TextureAsset> texture, TextureSemantic semantic) {
    require(bool(catalog), "Model texture requires a selected catalog");
    const auto selected = selected_member(*catalog, texture.id, TextureAsset::type);
    const auto variant = std::string(texture_variant_key(semantic));
    return pool.request(
        texture, selected.revision, selected.generation,
        [project = std::move(project), catalog = std::move(catalog), selected, texture,
         semantic](std::stop_token stop) {
            const auto family = load_selected(project, *catalog, selected, stop);
            auto data =
                std::make_unique<TextureData>(model_texture_resource(family, texture, semantic));
            require(!stop.stop_requested(), "Model texture preparation cancelled");
            const auto bytes = data->resident_bytes();
            return ResourceCandidate<TextureAsset>{std::move(data), {bytes}};
        },
        {}, 0, variant);
}
ResourceTicket request_model_mesh(ResourcePool<MeshAsset>& pool, std::filesystem::path project,
                                  std::shared_ptr<const AssetCatalog> catalog,
                                  AssetRef<MeshAsset> mesh) {
    require(bool(catalog), "Model mesh requires a selected catalog");
    const auto selected = selected_member(*catalog, mesh.id, MeshAsset::type);
    return pool.request(mesh, selected.revision, selected.generation,
                        [project = std::move(project), catalog = std::move(catalog), selected,
                         mesh](std::stop_token stop) {
                            const auto family = load_selected(project, *catalog, selected, stop);
                            auto data = std::make_unique<MeshResourceData>(
                                model_mesh_resource(family, mesh));
                            require(!stop.stop_requested(), "Model mesh preparation cancelled");
                            const auto bytes = data->resident_bytes();
                            return ResourceCandidate<MeshAsset>{std::move(data), {bytes}};
                        });
}
ResourceTicket request_model_pbr_material(ResourcePool<MaterialAsset>& pool,
                                          std::filesystem::path project,
                                          std::shared_ptr<const AssetCatalog> catalog,
                                          AssetRef<MaterialAsset> material) {
    require(bool(catalog), "Model PBR material requires a selected catalog");
    const auto selected = selected_member(*catalog, material.id, MaterialAsset::type);
    return pool.request(
        material, selected.revision, selected.generation,
        [project = std::move(project), catalog = std::move(catalog), selected,
         material](std::stop_token stop) {
            const auto family = load_selected(project, *catalog, selected, stop);
            auto data = std::make_unique<MaterialResourceData>(
                model_pbr_material_resource(family, material));
            require(!stop.stop_requested(), "Model PBR material preparation cancelled");
            const auto bytes = data->resident_bytes();
            return ResourceCandidate<MaterialAsset>{std::move(data), {bytes}};
        },
        {}, 0, "builtin:gltf-pbr-v1");
}
ResourceTicket request_model_material(ResourcePool<MaterialAsset>& pool,
                                      std::filesystem::path project,
                                      std::shared_ptr<const AssetCatalog> catalog,
                                      AssetRef<MaterialAsset> material, MaterialLayout layout) {
    require(bool(catalog), "Model material requires a selected catalog");
    const auto selected = selected_member(*catalog, material.id, MaterialAsset::type);
    require(layout.model.size() <= 255 && layout.parameters.size() <= 256 &&
                layout.textures.size() <= 64,
            "Material layout exceeds profile bounds");
    nlohmann::json profile{{"model", layout.model},
                           {"parameters", nlohmann::json::object()},
                           {"textures", nlohmann::json::object()}};
    for (const auto& [key, value] : layout.parameters) {
        require(key.size() <= 255, "Material layout key too long");
        profile["parameters"][key] = unsigned(value);
    }
    for (const auto& [key, value] : layout.textures) {
        require(key.size() <= 255, "Material layout key too long");
        profile["textures"][key] = {unsigned(value.semantic), unsigned(value.dimension),
                                    value.required};
    }
    // Different reflected layouts must never coalesce and skip compatibility.
    const auto variant = "layout:" + asset_build_digest(profile);
    return pool.request(
        material, selected.revision, selected.generation,
        [project = std::move(project), catalog = std::move(catalog), selected, material,
         layout = std::move(layout)](std::stop_token stop) {
            const auto family = load_selected(project, *catalog, selected, stop);
            auto data = std::make_unique<MaterialResourceData>(
                model_material_resource(family, material, layout));
            require(!stop.stop_requested(), "Model material preparation cancelled");
            const auto bytes = data->resident_bytes();
            return ResourceCandidate<MaterialAsset>{std::move(data), {bytes}};
        },
        {}, 0, variant);
}
} // namespace forge::asset_detail
