#pragma once
#include <forge/scene.hpp>
#include <thread>
namespace forge {
// Scene commands are shared by UI gestures, palette and isolated automation sessions.
// All commands validate on a candidate; one successful batch is one authored undo step.
Json authoring_commands();
Json apply_authoring(Scene& scene, const Json& commands, std::uint64_t expected_revision);
Json authoring_command(Scene& scene, const std::string& operation,
                       const Json& arguments = Json::object());
inline bool authoring_history(Scene& scene, bool redo) {
    return redo ? scene.redo() : scene.undo();
}
Json scene_diagnostics(const Scene& scene);
class AuthoringSession {
  public:
    explicit AuthoringSession(Scene& scene);
    Json handle(const Json& request);
    Json target() const;

  private:
    Scene& scene_;
    std::thread::id owner_;
    std::string session_;
};
} // namespace forge
