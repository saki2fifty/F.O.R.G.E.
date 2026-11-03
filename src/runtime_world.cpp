#include "animation_playback_access.hpp"
#include "application_state_resource.hpp"
#include "particle_runtime.hpp"
#include "runtime_property_tune.hpp"
#include "runtime_tile_map.hpp"
#include <exception>
#include <forge/animation.hpp>
#include <forge/application_states.hpp>
#include <forge/audio.hpp>
#include <forge/audio_components.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_components.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/runtime_world.hpp>
namespace forge {
RuntimeWorld::RuntimeWorld(std::vector<EngineModule> modules, PhysicsConfig physics,
                           const std::optional<AudioConfig>& audio,
                           const std::filesystem::path& project, bool ui)
    : engine(WorldRole::Runtime, false,
             [&] {
                 if (!project.empty())
                     modules.push_back(runtime_resources_module(project));
                 modules.push_back(physics_module(physics, project));
                 modules.push_back(animation_module(project));
                 modules.push_back(navigation_module(project));
                 modules.push_back(particle_module(project));
                 if (ui)
                     modules.push_back(ui_module(project));
                 if (audio)
                     modules.push_back(audio_module(*audio));
                 return std::move(modules);
             }()),
      scene(engine.world()), simulation(engine.world(), scene) {}
std::shared_ptr<PhysicsRuntime> RuntimeWorld::physics() {
    return std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
}
Json RuntimeWorld::tune_property(EntityRef target, const std::string& type,
                                 const std::string& property, const Json& value) {
    auto& context = engine.world();
    if (!context.on_owner_thread() || context.role() != WorldRole::Runtime ||
        context.world().is_readonly())
        throw std::runtime_error("Live property tuning requires the writable runtime owner");
    if (target.scene != scene.asset_id())
        throw std::runtime_error("Live property targets a different scene asset");
    if (RuntimePropertyTune::camera_shot_type(type)) {
        if (!RuntimePropertyTune::camera_shot_property(property) ||
            !engine.services().available(Capability::Animation))
            throw std::runtime_error(
                "Only an admitted Camera Shot blend duration can be tuned live");
        return AnimationPlaybackAccess::tune_camera_shot(
            *std::static_pointer_cast<AnimationRuntime>(engine.services().animation()), target,
            value);
    }
    if (AnimationPlaybackAccess::rig_type(type)) {
        if (!engine.services().available(Capability::Animation))
            throw std::runtime_error("Live IK tuning requires enabled animation");
        auto applied = AnimationPlaybackAccess::tune_rig(
            *std::static_pointer_cast<AnimationRuntime>(engine.services().animation()), target,
            type, property, value);
        simulation.sync_animation_pose();
        return applied;
    }
    if (RuntimePropertyTune::particle_type(type))
        return particle_runtime(context)->tune_appearance(target, property, value);
    if (RuntimePropertyTune::navigation_type(type)) {
        if (!engine.services().available(Capability::Navigation))
            throw std::runtime_error("Live navigation tuning requires enabled navigation");
        return std::static_pointer_cast<NavigationRuntime>(engine.services().navigation())
            ->tune(target, type, property, value);
    }
    if (type == "forge.audio_source" && !engine.services().available(Capability::Audio))
        throw std::runtime_error("Live audio tuning requires enabled audio");
    auto applied = scene.tune_runtime_property(target.entity.str(), type, property, value);
    if (RuntimePropertyTune::state_condition_type(type))
        simulation.reconcile_state_conditions();
    if (type == "forge.audio_source")
        std::static_pointer_cast<AudioRuntime>(engine.services().audio())->refresh_sources();
    return applied;
}
bool RuntimeWorld::prepare_physics_assets() {
    if (engine.services().available(Capability::Resources))
        engine.services().resources()->synchronize();
    if (!prepare_application_states())
        return false;
    detail::synchronize_runtime_tile_maps(engine.world());
    if (!detail::prepare_runtime_tile_maps(engine.world()))
        return false;
    return physics()->prepare_assets();
}
bool RuntimeWorld::prepare_application_states(bool initialize) {
    AssetId selected;
    unsigned count = 0;
    engine.world().world().each([&](flecs::entity entity, const ApplicationStates& controller) {
        if (entity.has(flecs::Prefab) || !controller.definition.id ||
            !entity.has<SceneMember>(scene.membership()))
            return;
        ++count;
        selected = controller.definition.id;
    });
    if (count > 1)
        throw std::runtime_error(
            "One runtime World supports one configured Application States controller");
    if (application_states_ready_) {
        if (selected != application_states_asset_)
            throw std::runtime_error(
                "Application States controller changed after candidate admission");
        if (initialize)
            initialize_application_states(engine.world().world());
        return true;
    }
    const auto services = engine.services();
    if (!selected) {
        if (application_states_token_) {
            services.resources()->release(application_states_token_);
            application_states_token_ = 0;
        }
        if (initialize)
            initialize_application_states(engine.world().world());
        return true;
    }
    if (!services.available(Capability::Resources))
        throw std::runtime_error(
            "Application States requires the project's runtime resource owner");
    auto resources = services.resources();
    if (application_states_token_ && selected != application_states_asset_) {
        resources->release(application_states_token_);
        application_states_token_ = 0;
    }
    if (!application_states_token_) {
        application_states_token_ =
            resources->request(RuntimeResourceKind::ApplicationStates, selected);
        application_states_asset_ = selected;
    }
    const auto status = resources->inspect(application_states_token_);
    if (status.state == "failed" || status.state == "dependency failed" ||
        status.state == "cancelled")
        throw std::runtime_error("Application States preparation: " + status.diagnostic);
    if (status.state != "ready" || status.requested_revision != status.retained_revision)
        return false;
    const auto lease = resources->application_states(application_states_token_);
    if (!lease)
        throw std::runtime_error(
            "Application States ready subscription has no retained definition");
    configure_application_states(engine.world().world(), lease.get(), selected);
    if (initialize)
        initialize_application_states(engine.world().world());
    resources->release(application_states_token_);
    application_states_token_ = 0;
    application_states_ready_ = true;
    return true;
}
bool RuntimeWorld::prepare_scene_resources() {
    // Runtime-only readiness extracted from the legacy presentation candidate.
    // Mirrors Candidate::poll up to the point it begins touching the host's
    // GPU/UI surfaces. Physics collision assets are prepared by GameSession
    // before this helper is invoked; it does not tick, run control callbacks,
    // publish worlds, or touch GPU.
    if (engine.services().available(Capability::Resources))
        engine.services().resources()->synchronize();
    detail::synchronize_runtime_tile_maps(engine.world());
    if (!detail::prepare_runtime_tile_maps(engine.world()))
        return false;
    if (!animation_runtime(engine.world())->prepare_initial_pose())
        return false;
    if (!particle_runtime(engine.world())->prepare())
        return false;
    engine.world().evaluate_world_transforms();
    physics()->synchronize(0);
    simulation.reset_presentation();
    if (!prepare_audio_sources())
        return false;
    const auto services = engine.services();
    if (services.available(Capability::Navigation) &&
        !std::static_pointer_cast<NavigationRuntime>(services.navigation())->prepare_assets())
        return false;
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
bool RuntimeWorld::prepare_audio_sources() {
    simulation.sync_audio();
    auto services = engine.services();
    if (services.available(Capability::Audio)) {
        auto audio = std::static_pointer_cast<AudioRuntime>(services.audio());
        if (!audio || audio->status().at("failed_sources").get<std::size_t>())
            throw std::runtime_error("Scene preparation: required audio sources failed");
        if (audio->status().at("pending_sources").get<std::size_t>())
            return false;
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
    return true;
}
} // namespace forge
