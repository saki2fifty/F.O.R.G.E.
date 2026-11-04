#pragma once
#include "frame_renderer.hpp"
#include <forge/authoring.hpp>
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/render_scene.hpp>
#include <thread>
namespace forge::test_fog_frame {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& scratch) {
    const auto check = [](bool value, const char* text) {
        if (!value)
            throw std::runtime_error(text);
    };
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    WorldContext world;
    Scene scene(world);
    const auto entity = [&](const char* name, LocalTranslation at) {
        const auto id = authoring_command(scene, "entity.create", {{"name", name}, {"kind", 4}})
                            .at("selected")
                            .get<std::string>();
        auto value = scene.entity(id);
        value.set<Primitive>({4}).set<LocalTranslation>(at);
        return value;
    };
    entity("Left", {-1.5, 0, 0}).set<Primitive>({0}).set<Tint>({.8f, .2f, .1f});
    entity("Right", {1.5, 0, 0}).set<Primitive>({0}).set<Tint>({.2f, .8f, .1f});
    entity("Camera", {0, 1, -8}).set<Camera>({});
    FogVolume fog;
    fog.half_extent_x = fog.half_extent_y = 8;
    fog.half_extent_z = 1;
    fog.color_r = .1f;
    fog.color_g = .4f;
    fog.color_b = .8f;
    fog.density = .6f;
    entity("Fog", {0, 0, -3}).set<FogVolume>(fog);
    scene.save(root / "main.scene.json");
    AssetCatalog catalog(root);
    catalog.add_scene("main.scene.json");
    catalog.save(AssetCatalog::project_index(root));
    ProjectSettings settings(root);
    auto config = ProjectSettings::defaults("Fog Frame");
    config["game"] = default_game_settings("org.forge.fog-frame-test", "Fog Frame");
    config["startup_scene"] = {{"asset", scene.asset_id()}, {"source", "main.scene.json"}};
    settings.save(config);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(catalog));
    FrameRenderer game(presentation);
    game.resources(host);
    const auto render = [&](const RenderScene& source) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        std::vector<std::uint8_t> result;
        do {
            presentation.begin_frame();
            auto* output = game.game(context, source, 96, 64);
            check(output, "Fog Game frame did not produce a target");
            presentation.readback(context, output);
            presentation.end_frame(context);
            host->submit();
            context->WaitForIdle();
            const auto pixels = presentation.take_readback(context);
            check(pixels.has_value() && pixels->width == 96 && pixels->height == 64,
                  "Fog Game readback did not complete");
            result = pixels->rgba;
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline,
                  "Fog Game resource preparation timed out");
        } while (game.pending());
        if (!game.diagnostics().empty())
            throw std::runtime_error("Fog Game frame: " + game.diagnostics().front().text);
        return result;
    };
    auto source = extract_render_scene(scene.effective_document());
    check(source.fog_volumes.size() == 1 && source.meshes.size() == 2,
          "Fog Game fixture extraction changed");
    for (unsigned projection : {0u, 1u}) {
        source.cameras[0].camera.projection = projection;
        source.cameras[0].camera.orthographic_height = 6;
        auto clear = source;
        clear.fog_volumes.clear();
        const auto baseline = render(clear), fogged = render(source);
        check(baseline != fogged && render(clear) == baseline && render(source) == fogged,
              "Fog Game toggle retained stale native buffers or did not affect geometry");
        auto masked = source;
        masked.cameras[0].camera.layers = 1;
        for (auto& mesh : masked.meshes)
            mesh.renderer.layers = 1;
        masked.fog_volumes[0].volume.layers = 2;
        auto masked_clear = masked;
        masked_clear.fog_volumes.clear();
        check(render(masked) == render(masked_clear),
              "Fog Game ignored camera/geometry layer isolation");
        masked.fog_volumes[0].volume.layers = 1;
        check(render(masked) != render(masked_clear), "Fog Game shared layer lost its medium");
        auto disabled = source;
        disabled.fog_volumes[0].volume.enabled = false;
        check(render(disabled) == baseline, "Fog Game retained a disabled volume");
    }
    context->WaitForIdle();
    std::cout << "Fog actual Game/Bundle/renderer: perspective/orthographic, "
                 "immutable-empty/active toggles and camera/geometry layers passed\n";
}
} // namespace forge::test_fog_frame
