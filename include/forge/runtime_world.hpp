#pragma once
#include <forge/runtime.hpp>
namespace forge {
// Runtime composition shared by the isolated Play worker and standalone hosts.
// Destruction stops simulation before Scene, then finalizes the world while
// engine-module code leases remain alive.
class RuntimeWorld {
  public:
    RuntimeWorld(std::vector<EngineModule>, PhysicsConfig, const std::optional<AudioConfig>&,
                 const std::filesystem::path&, bool ui);
    std::shared_ptr<PhysicsRuntime> physics();
    // Runtime-only readiness extracted from the legacy presentation candidate.
    // Returns true once the candidate world is internally consistent for
    // gameplay tick 0. Physics collision assets must already be prepared by
    // GameSession before this is invoked; this method does not tick, run
    // control callbacks, publish worlds, or touch GPU.
    bool prepare_scene_resources();
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
};
} // namespace forge
