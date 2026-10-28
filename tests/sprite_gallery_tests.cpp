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
void playback(const std::filesystem::path& root, const AssetCatalog& catalog, AssetId scene_id,
              unsigned sprite_count = 5) {
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
    auto original = scene.entity(animated.str()).get<SpriteRenderer>();
    const auto expected = [](double time) { return unsigned(std::fmod(time, 2.) / .5); };
    for (unsigned tick = 1; tick <= 12; ++tick) {
        if (tick == 5 && original.region_enabled) {
            ++original.region_x;
            scene.tune_runtime_property(animated.str(), "forge.sprite_renderer", "region_x",
                                        original.region_x);
        }
        simulation.tick(.25f);
        auto sampled = original;
        sampled.frame = expected(tick * .25);
        check(scene.entity(animated.str()).get<SpriteRenderer>() == sampled,
              "Gallery fixed Sprite animation differs from independent frame schedule");
        for (const auto alpha : {0., .5, 1.}) {
            const auto rendered = extract_render_scene(simulation.presentation(alpha));
            check(rendered.diagnostics.empty() && rendered.sprites.size() == sprite_count &&
                      rendered.cameras.size() == 1,
                  "Gallery Sprite animation lost rendered components");
            for (const auto& sprite : rendered.sprites)
                if (sprite.entity == animated) {
                    sampled.frame = expected((tick - 1) * .25 + .25 * alpha);
                    check(sprite.renderer == sampled,
                          "Gallery presentation interpolated/rounded a frame");
                }
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
        const auto regions = asset(catalog, "Scenes/78-sprite-image-regions.scene.json");
        const auto banks = asset(catalog, "Scenes/87-sprite-frame-regions.scene.json");
        const auto touch = asset(catalog, "Scenes/88-touch-input.scene.json");
        const auto gestures = asset(catalog, "Scenes/89-touch-gestures.scene.json");
        const auto single_touch = asset(catalog, "Scenes/91-single-touch-gestures.scene.json");
        const auto gamepads = asset(catalog, "Scenes/92-multiple-gamepads.scene.json");
        const auto motion = asset(catalog, "Scenes/93-gamepad-motion.scene.json");
        const std::array roots{basics, frames,   panels,       regions,  banks,
                               touch,  gestures, single_touch, gamepads, motion};
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
        std::map<EntityId, SpriteRenderer> fill_values, region_values, bank_values, touch_values,
            gesture_values, single_values, gamepad_values, motion_values;
        for (const auto id : roots) {
            scene.load(root / catalog.records().at(id).source);
            const auto rendered = extract_render_scene(scene.effective_document());
            check(rendered.diagnostics.empty() && rendered.meshes.empty() &&
                      rendered.sprites.size() ==
                          (id == motion                                          ? 2u
                           : id == gamepads                                      ? 4u
                           : id == touch || id == gestures || id == single_touch ? 1u
                           : id == basics || id == regions || id == banks        ? 6u
                                                                                 : 5u) &&
                      rendered.cameras.size() == 1,
                  "Gallery Sprite Scene did not use typed extraction");
            if (id == gestures)
                for (const auto& sprite : rendered.sprites)
                    gesture_values.emplace(sprite.entity, sprite.renderer);
            if (id == single_touch)
                for (const auto& sprite : rendered.sprites)
                    single_values.emplace(sprite.entity, sprite.renderer);
            if (id == gamepads)
                for (const auto& sprite : rendered.sprites)
                    gamepad_values.emplace(sprite.entity, sprite.renderer);
            if (id == motion)
                for (const auto& sprite : rendered.sprites)
                    motion_values.emplace(sprite.entity, sprite.renderer);
            if (id == touch)
                for (const auto& sprite : rendered.sprites)
                    touch_values.emplace(sprite.entity, sprite.renderer);
            if (id == panels)
                for (const auto& sprite : rendered.sprites)
                    fill_values.emplace(sprite.entity, sprite.renderer);
            if (id == regions)
                for (const auto& sprite : rendered.sprites)
                    region_values.emplace(sprite.entity, sprite.renderer);
            if (id == banks)
                for (const auto& sprite : rendered.sprites)
                    bank_values.emplace(sprite.entity, sprite.renderer);
        }
        check(region_values.size() == 6 &&
                  std::count_if(region_values.begin(), region_values.end(),
                                [](const auto& entry) { return entry.second.region_enabled; }) == 5,
              "Gallery image region experiment lost its whole-image comparison or five crops");
        playback(root, catalog, frames);
        playback(root, catalog, regions, 6);
        playback(root, catalog, banks, 6);
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
        // Scene roots share seven existing image/material/animation assets.
        check(loaded.records().size() == roots.size() + 7,
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
        playback(relocated, loaded, regions, 6);
        playback(relocated, loaded, banks, 6);
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
        restored.restore_snapshot(load_game_scene(relocated, {regions}));
        std::map<EntityId, SpriteRenderer> packed_regions;
        for (const auto& sprite : extract_render_scene(restored.effective_document()).sprites)
            packed_regions.emplace(sprite.entity, sprite.renderer);
        check(packed_regions == region_values,
              "Relocated source-free content lost image region values or identities");
        restored.restore_snapshot(load_game_scene(relocated, {banks}));
        std::map<EntityId, SpriteRenderer> packed_banks;
        for (const auto& sprite : extract_render_scene(restored.effective_document()).sprites)
            packed_banks.emplace(sprite.entity, sprite.renderer);
        check(packed_banks == bank_values &&
                  std::count_if(
                      packed_banks.begin(), packed_banks.end(),
                      [](const auto& row) { return !row.second.frame_regions.empty(); }) == 5,
              "Relocated content lost ordered arbitrary frame regions");
        restored.restore_snapshot(load_game_scene(relocated, {touch}));
        std::map<EntityId, SpriteRenderer> packed_touch;
        for (const auto& sprite : extract_render_scene(restored.effective_document()).sprites)
            packed_touch.emplace(sprite.entity, sprite.renderer);
        check(touch_values.size() == 1 && packed_touch == touch_values,
              "Relocated source-free content lost original touch canvas or marker");
        restored.restore_snapshot(load_game_scene(relocated, {gestures}));
        std::map<EntityId, SpriteRenderer> packed_gestures;
        for (const auto& sprite : extract_render_scene(restored.effective_document()).sprites)
            packed_gestures.emplace(sprite.entity, sprite.renderer);
        check(gesture_values.size() == 1 && packed_gestures == gesture_values,
              "Relocated source-free content lost original gesture canvas or marker");
        restored.restore_snapshot(load_game_scene(relocated, {single_touch}));
        std::map<EntityId, SpriteRenderer> packed_single;
        for (const auto& sprite : extract_render_scene(restored.effective_document()).sprites)
            packed_single.emplace(sprite.entity, sprite.renderer);
        check(single_values.size() == 1 && packed_single == single_values,
              "Relocated source-free content lost the single-touch canvas or marker");
        restored.restore_snapshot(load_game_scene(relocated, {gamepads}));
        std::map<EntityId, SpriteRenderer> packed_gamepads;
        for (const auto& sprite : extract_render_scene(restored.effective_document()).sprites)
            packed_gamepads.emplace(sprite.entity, sprite.renderer);
        check(gamepad_values.size() == 4 && packed_gamepads == gamepad_values,
              "Relocated source-free content lost the four-player colors, regions or identities");
        restored.restore_snapshot(load_game_scene(relocated, {motion}));
        std::map<EntityId, SpriteRenderer> packed_motion;
        for (const auto& sprite : extract_render_scene(restored.effective_document()).sprites)
            packed_motion.emplace(sprite.entity, sprite.renderer);
        check(motion_values.size() == 2 && packed_motion == motion_values,
              "Relocated source-free content lost the raw-motion canvas or either marker");
        std::cout << "Gallery71/72/73/78 real texture/material/region/fill/animation consumers and "
                     "relocated "
                     "source-free content closure passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
