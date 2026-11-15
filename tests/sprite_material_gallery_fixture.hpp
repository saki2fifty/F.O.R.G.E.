#pragma once
#include "asset_bytes.hpp"
#include <array>
#include <forge/assets.hpp>
#include <forge/material_source.hpp>
#include <forge/render_scene.hpp>
#include <forge/scene.hpp>
#include <forge/shader_asset.hpp>
#include <fstream>
#include <set>

namespace forge::test_sprite_material_gallery {
inline constexpr auto scene_path = "Scenes/215-programmable-sprite-materials.scene.json";
inline constexpr auto image_path = "Assets/Sprites/Badge.png";
inline constexpr std::array shaders{"Assets/Sprites/Materials/Image.shader.json",
                                    "Assets/Sprites/Materials/Tint.shader.json"};
inline constexpr std::array materials{"Assets/Sprites/Materials/Image.material.json",
                                      "Assets/Sprites/Materials/Tint.material.json"};
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline AssetId asset(const AssetCatalog& catalog, const std::filesystem::path& path) {
    for (const auto& [id, record] : catalog.records())
        if (!record.subasset && record.source == path)
            return id;
    throw std::runtime_error("Missing Sprite Material Gallery asset: " + path.generic_string());
}
inline Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
inline void inspect(const std::filesystem::path& root) {
    const auto catalog = AssetCatalog::open_project(root);
    for (unsigned i = 0; i < shaders.size(); ++i) {
        const auto document = read(root / shaders[i]);
        const auto program = shader_program_source(document);
        check(program.surface && program.surface->sprite_image && !program.surface->ui_effect &&
                  !program.surface->physically_based,
              "Gallery Shader lost its Sprite-only interface");
        check(i ? document.contains("graph") : document.contains("source_root"),
              "Gallery Shader lost its graph or HLSL source form");
        const auto material = MaterialSource::parse(
            asset_detail::read_bytes(root / materials[i], material_source_byte_limit));
        const auto& overrides = material.document.at("overrides");
        check(material.asset() == asset(catalog, materials[i]) &&
                  overrides.at("shader").get<AssetId>() == asset(catalog, shaders[i]) &&
                  overrides.at("textures")
                          .at(*program.surface->sprite_image)
                          .at("asset")
                          .get<AssetId>() == asset(catalog, image_path),
              "Gallery Material lost its durable Shader or primary Texture identity");
    }
    WorldContext world;
    Scene scene(world);
    scene.load(root / scene_path);
    const auto rendered = extract_render_scene(scene.effective_document());
    check(scene.asset_id() == asset(catalog, scene_path) && rendered.sprites.size() == 4 &&
              rendered.cameras.size() == 1 && rendered.meshes.empty() && rendered.lights.empty() &&
              rendered.diagnostics.empty(),
          "Gallery Scene lost its four Sprites or active Camera");
    std::set<unsigned> modes;
    unsigned filtered = 0;
    for (const auto& sprite : rendered.sprites) {
        check(sprite.renderer.material.id == asset(catalog, materials[0]) ||
                  sprite.renderer.material.id == asset(catalog, materials[1]),
              "Gallery Sprite uses an unrelated Material");
        modes.insert(sprite.renderer.image_mode);
        filtered += sprite.renderer.mip_filter == unsigned(SpriteMipFilter::Linear);
    }
    check(modes == std::set<unsigned>{unsigned(SpriteImageMode::Simple),
                                      unsigned(SpriteImageMode::Sliced),
                                      unsigned(SpriteImageMode::Tiled)} &&
              filtered == 2,
          "Gallery Scene lost its image modes or filtered frame comparisons");
}
} // namespace forge::test_sprite_material_gallery
