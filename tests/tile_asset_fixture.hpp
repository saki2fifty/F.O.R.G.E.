#pragma once
#include "asset_storage.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include "tile_asset_authoring.hpp"
#include <chrono>
#include <forge/engine_texture_assets.hpp>
#include <forge/scene.hpp>

namespace forge::test_tile_asset_io {
inline void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
inline AssetImportOutcome publish(AssetImportService& service, const std::filesystem::path& path,
                                  AssetId id, bool map) {
    service.submit(
        service.prepare(path, {}, id),
        [map](auto& candidate, const auto& plan, const auto&) {
            if (map)
                prepare_tile_map_publication(candidate, plan);
            else
                prepare_tile_set_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(service.wait_idle(std::chrono::seconds(30)), "Tile asset import timed out");
    auto results = service.poll();
    check(results.size() == 1, "Tile asset publication receipt missing");
    return std::move(results[0]);
}
inline AssetId material(const std::filesystem::path& root, std::shared_ptr<const ProjectLease> lease) {
    const auto id = AssetId::generate();
    auto source = MaterialSource::create(id);
    source.document["overrides"]["model"] = "forge.gltf.unlit.v1";
    MaterialData values;
    values.model = "forge.gltf.unlit.v1";
    auto& slot = values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = TextureFilter::Nearest;
    source.document["overrides"]["textures"]["baseColorTexture"] = {
        {"asset", engine_texture(EngineTexture::Checker)},
        {"slot", material_values_document(values).at("textures").at("baseColorTexture")}};
    source.validate();
    atomic_write(root / "Assets/Atlas.material.json", source.document.dump(2));
    AssetImportService service(lease, material_import_registry(), {"linux", "none", "cpu"});
    service.submit(
        service.prepare("Assets/Atlas.material.json", {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_material_publication(candidate, plan);
        },
        [](const auto&, const auto& artifact) {
            (void)asset_detail::decode_material_bundle(artifact.files);
        });
    check(service.wait_idle(std::chrono::seconds(15)), "Fixture Material publication timed out");
    const auto results = service.poll();
    check(results.size() == 1 && results[0].published,
          "Fixture Material publication failed" +
              (results.empty() ? std::string{} : ": " + results[0].diagnostic));
    return id;
}
} // namespace forge::test_tile_asset_io
