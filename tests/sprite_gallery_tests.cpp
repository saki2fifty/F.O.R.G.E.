#include "asset_bytes.hpp"
#include "material_selection.hpp"
#include "runtime_package.hpp"
#include "sprite_material_profile.hpp"
#include "texture_bundle_validation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/transform_animation.hpp>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
AssetId asset(const AssetCatalog& catalog, const std::filesystem::path& source) {
    for (const auto& [id, row] : catalog.records())
        if (!row.subasset && row.source == source)
            return id;
    throw std::runtime_error("Sprite Gallery asset missing: " + path_utf8(source));
}
CachedArtifact selected(const std::filesystem::path& root, const AssetRecord& record) {
    return DerivedDataCache(root).load_selected(
        record.metadata.at("forge.import").at("key").get<std::string>(), [](const auto&) {});
}
void playback(const std::filesystem::path& root, const AssetCatalog& catalog, AssetId scene_id) {
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {scene_id}));
    RuntimeSimulation simulation(engine.world(), scene);
    const auto animation = animation_runtime(engine.world());
    animation->catalog(std::make_shared<const AssetCatalog>(catalog));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery Sprite animation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    EntityId animated;
    const auto effective = scene.effective_document();
    for (const auto& row : effective.at("entities"))
        if (row.at("components").contains("forge.transform_animator"))
            animated = row.at("id").get<EntityId>();
    check(bool(animated), "Gallery frame animator missing");
    const auto expected = [](double time) { return unsigned(std::fmod(time, 2.) / .5); };
    for (unsigned tick = 1; tick <= 12; ++tick) {
        simulation.tick(.25f);
        check(scene.entity(animated.str()).get<SpriteRenderer>().frame == expected(tick * .25),
              "Gallery fixed Sprite animation differs from independent frame schedule");
        for (const auto alpha : {0., .5, 1.}) {
            const auto rendered = extract_render_scene(simulation.presentation(alpha));
            check(rendered.diagnostics.empty() && rendered.sprites.size() == 5 &&
                      rendered.cameras.size() == 1,
                  "Gallery Sprite animation lost rendered components");
            for (const auto& sprite : rendered.sprites)
                if (sprite.entity == animated)
                    check(sprite.renderer.frame == expected((tick - 1) * .25 + .25 * alpha),
                          "Gallery presentation interpolated/rounded a frame");
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and Sprite package scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "authored project";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(std::filesystem::absolute(argv[1]), root,
                              std::filesystem::copy_options::recursive);
        auto catalog = AssetCatalog::open_project(root);
        const auto basics = asset(catalog, "Scenes/71-world-sprites.scene.json");
        const auto frames = asset(catalog, "Scenes/72-sprite-frame-animation.scene.json");
        const auto panels = asset(catalog, "Scenes/73-sprite-image-fill.scene.json");
        const std::array roots{basics, frames, panels};
        const auto badge = asset(catalog, "Assets/Sprites/Badge.png");
        const auto sheet = asset(catalog, "Assets/Sprites/SignalSheet.png");
        const auto badge_material = asset(catalog, "Assets/Sprites/Badge.material.json");
        const auto sheet_material = asset(catalog, "Assets/Sprites/SignalSheet.material.json");
        const auto panel = asset(catalog, "Assets/Sprites/Panel.png");
        const auto panel_material = asset(catalog, "Assets/Sprites/Panel.material.json");
        for (const auto id : {badge_material, sheet_material, panel_material}) {
            const auto selection = asset_detail::load_material_selection(root, catalog, {id});
            validate_sprite_material_profile(selection.data.values);
            check(selection.data.textures.at("baseColorTexture").id == (id == badge_material ? badge
                                                                        : id == sheet_material
                                                                            ? sheet
                                                                            : panel),
                  "Gallery Sprite material lost its real image dependency");
        }
        for (const auto id : {badge, sheet, panel}) {
            const auto cooked = selected(root, catalog.records().at(id));
            const auto index = asset_detail::validate_texture_bundle(cooked.files);
            bool color = false;
            for (const auto& variant : index.variants) {
                if (variant.semantic != TextureSemantic::Color)
                    continue;
                const auto file =
                    std::find_if(cooked.files.begin(), cooked.files.end(),
                                 [&](const auto& f) { return f.name == variant.file; });
                const auto image = decode_texture(file->bytes);
                validate_sprite_texture_profile(image);
                check(image.width == (id == badge   ? 32u
                                      : id == sheet ? 64u
                                                    : 96u) &&
                          image.height == (id == badge   ? 32u
                                           : id == sheet ? 16u
                                                         : 64u) &&
                          image.alpha == TextureAlpha::Straight,
                      "Original Gallery RGBA image lost dimensions/straight alpha");
                color = true;
            }
            check(color, "Gallery Sprite image has no admitted color variant");
        }
        EngineContext authored;
        Scene scene(authored.world());
        std::map<EntityId, SpriteRenderer> fill_values;
        for (const auto id : roots) {
            scene.load(root / catalog.records().at(id).source);
            const auto rendered = extract_render_scene(scene.effective_document());
            check(rendered.diagnostics.empty() && rendered.meshes.empty() &&
                      rendered.sprites.size() == (id == basics ? 6u : 5u) &&
                      rendered.cameras.size() == 1,
                  "Gallery Sprite Scene did not use typed extraction");
            if (id == panels)
                for (const auto& sprite : rendered.sprites)
                    fill_values.emplace(sprite.entity, sprite.renderer);
        }
        playback(root, catalog, frames);
        {
            ProjectLease lease(root);
            prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
        }
        const auto package = scratch / "content package";
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        std::filesystem::remove_all(root); // Only this test's private authored copy.
        const auto relocated = scratch / "relocated sprite content";
        std::filesystem::rename(package, relocated);
        const auto loaded = open_runtime_content(relocated, {"windows", "d3d12"});
        check(loaded.records().size() == 10,
              "Sprite content dependency closure is incomplete or oversized");
        for (const auto& file : std::filesystem::recursive_directory_iterator(relocated)) {
            if (!file.is_regular_file())
                continue;
            const auto name = path_utf8(file.path().filename());
            check(!name.ends_with(".png") && !name.ends_with(".material.json") &&
                      !name.ends_with(".animation.json") && !name.ends_with(".cpp") &&
                      !name.ends_with(".hpp") && !name.ends_with(".exe"),
                  "Sprite content package leaked image/material/clip/compiler/editor source");
        }
        for (const auto id : {badge_material, sheet_material, panel_material})
            validate_sprite_material_profile(
                asset_detail::load_material_selection(relocated, loaded, {id}).data.values);
        playback(relocated, loaded, frames);
        EngineContext runtime(WorldRole::Runtime);
        Scene restored(runtime.world());
        restored.restore_snapshot(load_game_scene(relocated, {panels}));
        const auto rendered = extract_render_scene(restored.effective_document());
        std::map<EntityId, SpriteRenderer> packed_values;
        for (const auto& sprite : rendered.sprites)
            packed_values.emplace(sprite.entity, sprite.renderer);
        check(rendered.diagnostics.empty() && fill_values.size() == 5 &&
                  packed_values == fill_values,
              "Relocated source-free content lost Sprite fill intent or identities");
        std::cout << "Gallery71/72/73 real texture/material/fill/animation consumers and relocated "
                     "source-free content closure passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
