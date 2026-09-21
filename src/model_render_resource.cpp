#include "model_render_resource.hpp"
#include "engine_render_resource.hpp"
#include "pbr_material.hpp"
#include "texture_bundle_validation.hpp"
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
    if (selected.index.version >= 3) {
        MeshModelBindings bindings{selected.owner, selected.revision, {}, {}};
        std::map<std::size_t, AssetId> node_assets;
        for (const auto& value : selected.index.members)
            if (value.node)
                require(
                    node_assets.emplace(*value.node, selected.bindings.at(value.identity.address))
                        .second,
                    "Model mesh has duplicate node identities");
        const auto& hierarchy = selected.index.hierarchy;
        const auto* animation =
            hierarchy.contains("animation") ? &hierarchy.at("animation").at("plan") : nullptr;
        std::map<std::size_t, std::size_t> skin_indices;
        const auto& nodes = hierarchy.at("nodes");
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            const auto& node = nodes[index];
            if (node.at("mesh").is_null() || node.at("mesh") != member.identity.address)
                continue;
            MeshModelNodeBinding binding;
            binding.node = node_assets.at(index);
            binding.morph_weights = node.at("weights").empty()
                                        ? result.mesh.morph_defaults
                                        : node.at("weights").get<std::vector<float>>();
            binding.visible = node.value("visible", true);
            binding.selectable = node.value("selectable", true);
            if (animation && !animation->at("node_skins").at(index).is_null()) {
                const auto source_skin = animation->at("node_skins").at(index).get<std::size_t>();
                auto [found, inserted] = skin_indices.emplace(source_skin, bindings.skins.size());
                if (inserted) {
                    MeshSkinBinding skin;
                    const auto& source = animation->at("skins").at(source_skin);
                    const auto& joint_nodes = animation->at("joint_nodes");
                    for (std::size_t joint = 0; joint < source.at("joints").size(); ++joint) {
                        const auto rig_index = source.at("joints")[joint].get<std::size_t>();
                        skin.joints.push_back(
                            node_assets.at(joint_nodes.at(rig_index).get<std::size_t>()));
                        AffineTransform inverse;
                        const auto& matrix = source.at("inverse_bind_matrices").at(joint);
                        for (unsigned row = 0; row < 3; ++row)
                            for (unsigned col = 0; col < 4; ++col)
                                inverse.m[row * 4 + col] = matrix.at(col * 4 + row).get<double>();
                        skin.inverse_bind.push_back(inverse);
                    }
                    bindings.skins.push_back(std::move(skin));
                }
                binding.skin = found->second;
            }
            bindings.nodes.push_back(std::move(binding));
        }
        result.model = std::move(bindings);
    }
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
ResourceTicket request_texture(ResourcePool<TextureAsset>& pool, std::filesystem::path project,
                               std::shared_ptr<const AssetCatalog> catalog,
                               AssetRef<TextureAsset> texture,
                               std::optional<TextureSemantic> semantic) {
    require(bool(catalog), "Texture requires a selected catalog");
    const auto found = catalog->records().find(texture.id);
    require(found != catalog->records().end() && found->second.type == TextureAsset::type,
            "Texture selection is missing or has the wrong type");
    const bool member = bool(found->second.subasset);
    const auto& imported = found->second.metadata.at("forge.import");
    const auto selected = member ? selected_member(*catalog, texture.id, TextureAsset::type)
                                 : Selected{texture.id, imported.at("key").get<std::string>(),
                                            imported.at("generation").get<std::uint64_t>()};
    require(imported.at("generation").is_number_unsigned() && selected.generation > 0,
            "Invalid texture publication generation");
    const auto variant = semantic ? std::string(texture_variant_key(*semantic)) : "color:auto";
    return pool.request(
        texture, selected.revision, selected.generation,
        [project = std::move(project), catalog = std::move(catalog), selected, texture, semantic,
         member](std::stop_token stop) {
            auto cancelled = [&] {
                require(!stop.stop_requested(), "Texture preparation cancelled");
            };
            cancelled();
            TextureBundleIndex index;
            std::shared_ptr<const CachedArtifact> artifact;
            if (member) {
                const auto family = load_selected(project, *catalog, selected, stop);
                index = decode_texture_bundle_index(family.bytes(family.member(texture.id)));
                artifact = family.artifact;
            } else {
                const auto& metadata =
                    catalog->records().at(texture.id).metadata.at("forge.import");
                require(metadata.at("version") == 1 &&
                            metadata.at("output_format") == "forge.texture-bundle" &&
                            metadata.at("output_version") == 1,
                        "Unsupported selected texture artifact profile");
                DerivedDataCache cache(project, {256 * 1024 * 1024, 512 * 1024 * 1024, 16});
                auto loaded = cache.load_selected(selected.revision, [&](const auto& candidate) {
                    cancelled();
                    index = validate_texture_bundle(candidate.files);
                });
                require(metadata.at("artifact_digest") ==
                            asset_build_digest(loaded.manifest.at("files")),
                        "Selected texture catalog and cooked revision disagree");
                const auto& inputs = loaded.manifest.at("inputs");
                require(inputs.at("source") == metadata.at("source_digest") &&
                            inputs.at("importer") == metadata.at("importer") &&
                            inputs.at("importer_revision") == metadata.at("importer_revision") &&
                            inputs.at("output_format") == metadata.at("output_format") &&
                            inputs.at("output_version") == metadata.at("output_version"),
                        "Texture artifact recipe differs from selected catalog revision");
                artifact = std::make_shared<const CachedArtifact>(std::move(loaded));
            }
            auto usage = semantic.value_or(TextureSemantic::Color);
            if (!semantic &&
                std::any_of(index.variants.begin(), index.variants.end(),
                            [](const auto& v) { return v.semantic == TextureSemantic::HdrColor; }))
                usage = TextureSemantic::HdrColor;
            const auto& entry = index.find(usage);
            for (const auto& file : artifact->files) {
                if (file.name != entry.file)
                    continue;
                auto data = std::make_unique<TextureData>(decode_texture(file.bytes));
                require(data->semantic == usage, "Selected texture semantic mismatch");
                cancelled();
                const auto bytes = data->resident_bytes();
                return ResourceCandidate<TextureAsset>{std::move(data), {bytes}};
            }
            throw std::runtime_error("Selected texture variant is missing");
        },
        {}, 0, variant);
}
ResourceTicket request_model_mesh(ResourcePool<MeshAsset>& pool, std::filesystem::path project,
                                  std::shared_ptr<const AssetCatalog> catalog,
                                  AssetRef<MeshAsset> mesh) {
    if (engine_asset(mesh.id))
        return request_engine_mesh(pool, mesh);
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
    if (engine_asset(material.id))
        return request_engine_material(pool, material);
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
