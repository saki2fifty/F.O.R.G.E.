#include "engine_render_resource.hpp"
#include "model_draw_candidate.hpp"
#include <forge/asset_build.hpp>
#include <forge/texture_bundle.hpp>
#include <iostream>
#include <set>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void finish(ModelDrawCandidate& candidate, ResourcePool<MeshAsset>& meshes,
            ResourcePool<MaterialAsset>& materials, ResourcePool<TextureAsset>& textures) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        meshes.pump();
        materials.pump();
        textures.pump();
        candidate.advance(1, meshes, materials, textures);
        if (candidate.state() != ResourceState::Loading)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Draw fallback candidate timed out");
}
} // namespace
int main() {
    try {
        auto catalog = std::make_shared<AssetCatalog>(std::filesystem::current_path());
        ResourcePool<MeshAsset> meshes;
        ResourcePool<MaterialAsset> materials;
        ResourcePool<TextureAsset> textures;
        std::set<AssetId> identities;
        for (const auto& asset : engine_assets()) {
            require(identities.insert(asset.id).second, "Engine asset identity collision");
            if (!engine_texture_asset(asset.id))
                require(engine_asset_revision(asset.id) ==
                            asset_build_digest(
                                {{"recipe", asset.primitive ? "forge-engine-primitive-v2"
                                                            : "forge-engine-material-v1"},
                                 {"asset", asset.id}}),
                        "Existing engine asset revision changed");
        }
        for (const auto& asset : engine_texture_assets()) {
            require(catalog->resolve(asset.id, TextureAsset::type).state == AssetState::Available &&
                        catalog->resolve(asset.id, MaterialAsset::type).state ==
                            AssetState::Incompatible,
                    "Built-in texture type admission failed");
            for (const auto semantic : {TextureSemantic::Color, TextureSemantic::Data,
                                        TextureSemantic::Normal, TextureSemantic::HdrColor}) {
                const auto data = engine_texture_resource({asset.id}, semantic);
                const auto decoded = decode_texture(encode_texture(data));
                require(decoded.dimension == asset.dimension && decoded.semantic == semantic &&
                            decoded.subresources == data.subresources,
                        "Engine texture dimension/semantic/payload did not round trip");
                const auto ticket = request_texture(textures, {}, catalog, {asset.id}, semantic);
                require(textures.wait(ticket, std::chrono::seconds(5)),
                        "Built-in texture did not load");
                const auto lease = textures.acquire(ticket);
                require(lease && lease.identity().asset == asset.id &&
                            lease.identity().revision == engine_asset_revision(asset.id),
                        "Engine texture resource lost its own identity/revision");
                const auto again = request_engine_texture(textures, {asset.id}, semantic);
                require(textures.wait(again, std::chrono::seconds(5)) &&
                            textures.acquire(again).identity() == lease.identity(),
                        "Repeated engine texture request did not share its realization");
            }
        }
        const auto missing = AssetId::generate();
        ModelDrawCandidate strict({}, catalog, 1, engine_primitive(0), {{"surface", {missing}}},
                                  meshes);
        finish(strict, meshes, materials, textures);
        require(strict.state() == ResourceState::DependencyFailed && !strict.ready(),
                "Strict replacement silently substituted a missing material");
        ModelDrawCandidate initial({}, catalog, 1, engine_primitive(0), {{"surface", {missing}}},
                                   meshes, {}, {}, true);
        finish(initial, meshes, materials, textures);
        const auto* ready = initial.ready();
        require(ready && ready->has_fallbacks() && !initial.diagnostic().empty() &&
                    ready->material_asset(missing) == engine_material(EngineMaterial::Error).id &&
                    !ready->materials.contains(missing) &&
                    ready->selection.parts[0][0].id == missing,
                "Initial material fallback erased authored selection or aliased its identity");
        const auto error = ready->materials.at(engine_material(EngineMaterial::Error).id);
        require(error->values.model == "forge.gltf.unlit.v1",
                "Error surface is not visible without lights");
        const auto original_key = prepared_model_draw_key(*ready, false);
        auto changed = *ready;
        changed.material_fallbacks.clear();
        require(prepared_model_draw_key(changed, false) != original_key,
                "Draw cache ignored physical fallback substitutions");
        for (const auto [semantic, role] :
             {std::pair{TextureSemantic::Color, "baseColorTexture"},
              std::pair{TextureSemantic::Normal, "normalTexture"},
              std::pair{TextureSemantic::Data, "metallicRoughnessTexture"}}) {
            auto preview = std::make_shared<MaterialPreviewSelection>();
            preview->asset = {AssetId::generate()};
            preview->revision = std::string(64, 'a');
            preview->generation = 1;
            preview->data = engine_material_resource(engine_material());
            preview->data.values.textures[role].semantic = semantic;
            preview->data.textures[role] = {AssetId::generate()};
            const auto original = preview->data.textures.at(role).id;
            ModelDrawCandidate strict_texture({}, catalog, 1, engine_primitive(0),
                                              {{"surface", preview->asset}}, meshes, preview);
            finish(strict_texture, meshes, materials, textures);
            require(!strict_texture.ready() &&
                        strict_texture.state() == ResourceState::DependencyFailed,
                    "Strict texture replacement silently substituted a fallback");
            ModelDrawCandidate candidate({}, catalog, 1, engine_primitive(0),
                                         {{"surface", preview->asset}}, meshes, preview, {}, true);
            finish(candidate, meshes, materials, textures);
            const auto* draw = candidate.ready();
            require(draw && !candidate.diagnostic().empty(),
                    "Initial missing texture did not fall back");
            const DrawTextureKey requested{original, semantic};
            const auto physical = draw->texture_asset(requested);
            const auto& lease = draw->textures.at(physical);
            require(
                physical.first != original && lease.identity().asset == physical.first &&
                    lease->semantic == semantic && lease->dimension == TextureDimension::D2 &&
                    !draw->textures.contains(requested) &&
                    draw->materials.at(preview->asset.id)->textures.at(role).id == original,
                "Texture fallback rewrote source intent, aliased identity or changed semantics");
            require(candidate.error_surface("native surface creation rejected", materials),
                    "Initial native failure did not prepare an error surface retry");
            finish(candidate, meshes, materials, textures);
            require(candidate.ready() && candidate.ready()->textures.empty() &&
                        !candidate.error_surface("second failure", materials),
                    "Error surface retry retained failed textures or retried indefinitely");
        }
        require(catalog->records().empty(),
                "Fallback realization wrote an engine asset into project metadata");
        // A selected catalog row can exist while its artifact is corrupt/absent.
        // That failure occurs asynchronously, after request_texture returns a ticket.
        auto broken_catalog = std::make_shared<AssetCatalog>(std::filesystem::current_path());
        const AssetRef<TextureAsset> broken{AssetId::generate()};
        AssetRecord record{broken.id, TextureAsset::type, "Assets/absent.png", 1, {}};
        record.metadata["forge.import"] = {{"version", 1},
                                           {"key", std::string(64, 'b')},
                                           {"generation", 1u},
                                           {"output_format", "forge.texture-bundle"},
                                           {"output_version", 1}};
        broken_catalog->add(record);
        auto preview = std::make_shared<MaterialPreviewSelection>();
        preview->asset = {AssetId::generate()};
        preview->revision = std::string(64, 'c');
        preview->generation = 1;
        preview->data = engine_material_resource(engine_material());
        preview->data.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        preview->data.textures["baseColorTexture"] = broken;
        ModelDrawCandidate failed_artifact({}, broken_catalog, 1, engine_primitive(0),
                                           {{"surface", preview->asset}}, meshes, preview, {},
                                           true);
        finish(failed_artifact, meshes, materials, textures);
        require(failed_artifact.ready() && failed_artifact.ready()->has_fallbacks() &&
                    !failed_artifact.diagnostic().empty() &&
                    broken_catalog->records().at(broken.id).metadata == record.metadata,
                "Asynchronous texture failure did not preserve catalog/realize fallback");
        ModelDrawCandidate stale({}, catalog, 1, engine_primitive(0), {}, meshes, {}, {}, true);
        stale.advance(2, meshes, materials, textures);
        require(stale.state() == ResourceState::Stale && !stale.ready(),
                "Fallback policy bypassed stale-candidate rejection");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
