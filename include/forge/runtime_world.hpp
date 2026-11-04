#pragma once
#include <forge/runtime.hpp>
namespace forge {
// Runtime composition shared by the isolated Play worker and standalone hosts.
// Destruction stops simulation before Scene, then finalizes the world while
// engine-module code leases remain alive.
class RuntimeWorld {
  public:
    RuntimeWorld(std::vector<EngineModule>, PhysicsConfig, const std::optional<AudioConfig>&,
                 const std::filesystem::path&, bool ui, RuntimeExecutionConfig execution = {});
    std::shared_ptr<PhysicsRuntime> physics();
    // Candidate CPU readiness before the first native realization. Map cells
    // stay owned by the existing Map/resource adapters; no gameplay time advances.
    bool prepare_physics_assets();
    bool application_states_pending() const { return application_states_token_ != 0; }
    // Configuration readiness, without simulation time. Recovery hosts pass false
    // until they can restore the admitted definition without replaying entry actions.
    bool prepare_application_states(bool initialize = true);
    // Runtime-only readiness extracted from the legacy presentation candidate.
    // Returns true once the candidate world is internally consistent for
    // gameplay tick 0. Physics collision assets must already be prepared by
    // GameSession before this is invoked; this method does not tick, run
    // control callbacks, publish worlds, or touch GPU.
    bool prepare_scene_resources();
    // Audio-only owner boundary, without elapsed time or animation resampling.
    bool prepare_audio_sources();
    // Shared live-property routing; each subsystem retains its validation and
    // mutation authority. Host generations remain checked by the protocol owner.
    Json tune_property(EntityRef, const std::string& type, const std::string& property,
                       const Json& value);
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;

  private:
    std::uint64_t application_states_token_ = 0;
    AssetId application_states_asset_;
    bool application_states_ready_ = false;
};
} // namespace forge
