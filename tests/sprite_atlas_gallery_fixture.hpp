#pragma once
#include "asset_bytes.hpp"
#include "material_authoring.hpp"
#include "model_render_resource.hpp"
#include "sprite_atlas_recipe.hpp"
#include "texture_authoring.hpp"
#include "transform_animation_authoring.hpp"
#include <cmath>
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <thread>

namespace forge::test_sprite_atlas_gallery {
inline constexpr auto source = "Scenes/127-source-aware-sprite-atlas.scene.json";
inline AssetId scene_id() { return AssetId::parse("ed3c5415-1ee7-49c2-9b74-78b20e9fb6f7"); }
inline AssetId atlas_id() { return AssetId::parse("60cdbe0f-f465-4e40-a089-18bb1f648af9"); }
inline AssetId material_id() { return AssetId::parse("4c3ea074-3145-47eb-b9d3-16270c66d242"); }
inline AssetId clip_id() { return AssetId::parse("4f1d42f1-9d52-43b0-88d6-babbed7344e9"); }
inline void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
inline std::filesystem::path prepare(const std::filesystem::path& gallery,
                                     const std::filesystem::path& scratch,
                                     const std::filesystem::path& worker) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str() / "Project";
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    const auto recipe = SpriteAtlasRecipe::parse(asset_detail::read_bytes(
        root / "Assets/Sprites/SourceAware.atlas.json", sprite_atlas_recipe_byte_limit));
    check(recipe.asset() == atlas_id() && recipe.next_id() == 100 && recipe.inputs().size() == 3,
          "Gallery atlas source lost its stable identity/keys");
    const auto catalog = AssetCatalog::open_project(root);
    auto lease = std::make_shared<ProjectLease>(root);
    // Only the three cooked consumers need this test target. Original input
    // images stay at their admitted immutable selections; they are build inputs,
    // not runtime dependencies of the resulting atlas Texture.
    for (unsigned type = 0; type < 3; ++type) {
        const auto id = type == 0 ? atlas_id() : type == 1 ? material_id() : clip_id();
        const auto registry = type == 0   ? texture_import_registry(worker)
                              : type == 1 ? material_import_registry()
                                          : transform_animation_import_registry();
        AssetImportService imports(lease, registry, desktop_texture_target());
        imports.submit(
            imports.prepare(catalog.records().at(id).source, {}, id),
            [type](auto& candidate, const auto& plan, const auto&) {
                if (type == 0)
                    prepare_texture_publication(candidate, plan);
                else if (type == 1)
                    prepare_material_publication(candidate, plan);
                else
                    prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(30)), "Gallery atlas publication stalled");
        const auto results = imports.poll();
        check(results.size() == 1 && results[0].published,
              results.empty() ? "Gallery atlas receipt missing" : results[0].diagnostic);
    }
    return root;
}
inline void ready(RuntimeWorld& world) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.prepare_physics_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery atlas resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
inline RenderScene sample(RuntimeWorld& world, unsigned tick) {
    auto frame = extract_render_scene(world.simulation.presentation(1));
    check(frame.diagnostics.empty() && frame.sprites.size() == 6 && frame.cameras.size() == 1 &&
              frame.meshes.empty(),
          "Gallery atlas lost its six Sprites/camera or acquired an unintended mesh");
    const std::array<std::uint32_t, 3> keys{7, 41, 99};
    unsigned stationary = 0, animated = 0;
    for (const auto& sprite : frame.sprites) {
        const auto& values = sprite.renderer;
        check(values.texture_frames && values.frame_regions.empty() && values.columns == 1 &&
                  values.rows == 1 && values.material.id == material_id(),
              "Gallery Sprite lost explicit Texture-frame intent");
        const auto column = unsigned(std::round(sprite.world.m[3] / 3.0 + 1));
        check(column < 3 && values.mip_filter == column, "Gallery atlas lost its mip mode columns");
        if (sprite.world.m[7] > 0) {
            ++stationary;
            check(values.frame == keys[column], "Gallery stationary Sprite key was retargeted");
        } else {
            ++animated;
            check(values.frame == keys[(tick / 60) % 3],
                  "Gallery sparse-key animation lost held frame timing");
        }
    }
    check(stationary == 3 && animated == 3, "Gallery atlas row composition changed");
    return frame;
}
} // namespace forge::test_sprite_atlas_gallery
