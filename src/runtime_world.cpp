#include <exception>
#include <forge/animation.hpp>
#include <forge/audio.hpp>
#include <forge/audio_components.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_components.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/runtime_world.hpp>
namespace forge {
RuntimeWorld::RuntimeWorld(Module& module, std::vector<EngineModule> modules, PhysicsConfig physics,
                           const std::optional<AudioConfig>& audio,
                           const std::filesystem::path& project, bool ui)
    : engine(WorldRole::Runtime, false,
             [&] {
                 if (!project.empty())
                     modules.push_back(runtime_resources_module(project));
                 modules.push_back(physics_module(physics, project));
                 modules.push_back(animation_module(project));
                 modules.push_back(navigation_module(project));
                 if (ui)
                     modules.push_back(ui_module(project));
                 if (audio)
                     modules.push_back(audio_module(*audio));
                 return std::move(modules);
             }()),
      scene(engine.world()), simulation(engine.world(), scene, module) {}
std::shared_ptr<PhysicsRuntime> RuntimeWorld::physics() {
    return std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
}
bool RuntimeWorld::prepare_scene_resources() {
    // Runtime-only readiness extracted from the legacy presentation candidate.
    // Mirrors Candidate::poll up to the point it begins touching the host's
    // GPU/UI surfaces. Physics collision assets are prepared by GameSession
    // before this helper is invoked; it does not tick, run control callbacks,
    // publish worlds, or touch GPU.
    if (!animation_runtime(engine.world())->prepare_initial_pose())
        return false;
    engine.world().evaluate_world_transforms();
    physics()->synchronize(0);
    simulation.reset_presentation();
    simulation.sync_audio();
    auto services = engine.services();
    if (services.available(Capability::Audio)) {
        auto audio = std::static_pointer_cast<AudioRuntime>(services.audio());
        if (!audio || audio->status().at("failed_sources").get<std::size_t>())
            throw std::runtime_error("Scene preparation: required audio sources failed");
    } else {
        // The Flecs iteration locks its table; throwing from inside the
        // callback leaves the lock held and aborts table teardown under
        // sanitizers. Observe first, then throw after the iteration returns.
        bool missing_audio = false;
        engine.world().world().each([&](flecs::entity e, const AudioSource&) {
            if (missing_audio)
                return;
            if (!e.has(flecs::Prefab))
                missing_audio = true;
        });
        if (missing_audio)
            throw std::runtime_error("Scene preparation: audio output is unavailable");
    }
    // Probe each enabled navigation dependency using the existing loader
    // and geometry-revision validation. Being off the mesh is not a load
    // failure. The Flecs iteration locks its table; capture the first
    // failure via std::exception_ptr and rethrow after the iteration so
    // no exception escapes the callback and leaves a locked table behind.
    {
        std::exception_ptr first_error;
        engine.world().world().each([&](flecs::entity e, const NavigationAgent& agent) {
            if (first_error || e.has(flecs::Prefab) || !agent.enabled)
                return;
            try {
                const auto result = services.navigation()->project_point(agent.navmesh, {});
                if (result.status == NavStatus::Missing || result.status == NavStatus::Stale ||
                    result.status == NavStatus::Invalid || result.status == NavStatus::Unavailable)
                    throw std::runtime_error("Scene preparation: " + result.diagnostic);
            } catch (...) {
                first_error = std::current_exception();
            }
        });
        if (first_error)
            std::rethrow_exception(first_error);
    }
    return true;
}
} // namespace forge
