#pragma once
#include <forge/scene.hpp>
#include <stdexcept>
#include <thread>
namespace forge {
class AuthoringError : public std::runtime_error {
  public:
    std::string code;
    AuthoringError(std::string value, const std::string& message)
        : std::runtime_error(message), code(std::move(value)) {}
};
// Scene commands are shared by UI gestures, palette and isolated automation sessions.
// Commands validate detached intent without a world; a successful batch is one undo step.
Json authoring_commands();
// Returns canonical detached intent; never commits or owns a world.
Json preview_authoring(const Scene& scene, const Json& commands);
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
    // Validate routing and optional revision without reading or mutating scene intent.
    // Owning editor workflow adapters share this boundary before invoking file owners.
    Json validate(const Json& request, bool require_revision) const;
    Json target() const;

  private:
    Scene& scene_;
    std::thread::id owner_;
    std::string session_;
};
} // namespace forge
