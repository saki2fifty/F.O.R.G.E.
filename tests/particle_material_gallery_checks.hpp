#pragma once
#include "particle_gpu_plan.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <set>
#include <thread>

namespace forge::test_particle_material_gallery {
inline constexpr const char* scene_path = "Scenes/255-compatible-material-particles.scene.json";
inline constexpr std::array names{"Teal", "Amber", "Violet", "Blue"};
inline std::filesystem::path asset_path(unsigned index, const char* suffix) {
    return std::filesystem::path("Assets/Particles/Materials") /
           (std::string(names.at(index)) + suffix);
}
inline void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
inline RuntimePackageTarget package_target() {
#ifdef _WIN32
    return {"windows", "d3d12"};
#else
    return {"linux", "none"};
#endif
}
// Exercise the ordinary runtime World after the authoring tree has been hidden.
// Return copied presentation only; no fixture owns a second emission clock.
inline RenderScene game(const std::filesystem::path& content, bool mixed_graph = false) {
    const auto catalog = open_runtime_content(content, package_target());
    require(catalog.records().size() == (mixed_graph ? 10u : 9u),
            "Palette Game lost its exact typed asset closure");
    for (unsigned i = 0; i < names.size(); ++i)
        for (const auto* suffix : {".material.json", ".particles.json"})
            require(!std::filesystem::exists(content / asset_path(i, suffix)),
                    "Palette Game retained editable asset sources");
    EngineContext runtime(WorldRole::Runtime, false, {particle_module(content)});
    Scene scene(runtime.world());
    std::filesystem::path saved_scene;
    for (const auto& [id, record] : catalog.records()) {
        (void)id;
        if (record.type == SceneAsset::type) {
            require(saved_scene.empty(), "Palette Game has ambiguous Scene roots");
            saved_scene = record.source;
        }
    }
    require(!saved_scene.empty(), "Palette Game has no saved Scene root");
    scene.load(content / saved_scene);
    const auto saved = scene.document();
    RuntimeSimulation simulation(runtime.world(), scene);
    const auto particles = particle_runtime(runtime.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!particles->prepare()) {
        require(std::chrono::steady_clock::now() < deadline, "Palette Game preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    for (unsigned frame = 0; frame < 12; ++frame)
        simulation.tick(.25f);
    auto render = extract_render_scene(simulation.presentation(1));
    require(render.diagnostics.empty() && render.particles.size() == 4 &&
                render.cameras.size() == 1 && render.lights.size() == 1 &&
                !render.settings.weighted_transparency && scene.document() == saved,
            "Palette Game lost its complete saved presentation");
    ResourcePool<ParticleEffectAsset> effects;
    std::set<AssetId> materials;
    unsigned graph_emitters = 0;
    for (const auto& emitter : render.particles) {
        const auto ticket =
            request_particle_effect(effects, content, std::make_shared<const AssetCatalog>(catalog),
                                    emitter.emitter.effect);
        require(effects.wait(ticket, std::chrono::seconds(15)), "Palette Game effect timed out");
        const auto selected = effects.acquire(ticket);
        const bool graph = selected && bool(selected->program);
        graph_emitters += graph;
        require(selected && selected->version == (mixed_graph && graph ? 49u : 48u) &&
                    particle_uses_gpu_material_palette(selected.get()) &&
                    selected->mesh == engine_primitive(8) && emitter.time == 3,
                "Palette Game lost its device profile, geometry or World clock");
        const auto packet = prepare_particle_gpu_values(
            selected.get(), emitter.time, emitter.emitter.seed, emitter.world, CameraView{},
            emitter.emitter.size_multiplier,
            {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b});
        require(packet.births[0] == 48 && !emitter.world_samples,
                "Palette Game lost its compact device birth window");
        bool refused = false;
        try {
            (void)particles->sample(scene.entity(emitter.entity.str()).id(), 1);
        } catch (const std::runtime_error&) {
            refused = true;
        }
        require(refused, "Palette Game substituted CPU particle sampling");
        materials.insert(selected->material.id);
    }
    require(materials.size() == 4, "Palette Game collapsed distinct Material selections");
    require(graph_emitters == (mixed_graph ? 1u : 0u),
            "Palette Game changed its built-in/Graph producer selection");
    return render;
}
} // namespace forge::test_particle_material_gallery
