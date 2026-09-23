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
} // namespace forge
