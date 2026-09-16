#pragma once
#include <forge/authoring.hpp>
#include <memory>
namespace forge {
// Explicitly enabled, loopback-only JSON-lines adapter. All scene access occurs
// in pump() on the owning thread. Transport identity is independent of API identity.
class LiveAuthoring {
  public:
    LiveAuthoring();
    ~LiveAuthoring();
    LiveAuthoring(const LiveAuthoring&) = delete;
    LiveAuthoring& operator=(const LiveAuthoring&) = delete;
    void start(Scene& scene, bool allow_edits);
    void stop();
    void pump(const std::string& unavailable_reason = {});
    bool active() const;
    bool editable() const;
    std::uint16_t port() const;
    Json connection() const;
    const std::string& status() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace forge
