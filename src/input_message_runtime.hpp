#pragma once
#include <forge/input.hpp>
#include <forge/scene.hpp>

namespace forge::detail {
// Transient fixed-tick projection. Flecs owns types, values and observer dispatch;
// Scene and RuntimeInput retain all authoring and input authority.
class InputMessageRuntime {
  public:
    InputMessageRuntime(WorldContext& context, Scene& scene) : context_(context), scene_(scene) {}
    void tick(const InputSnapshot&, const InputMap&);
    const Json& status() const { return status_; }

  private:
    WorldContext& context_;
    Scene& scene_;
    Json status_ = {{"tick", 0},
                    {"emitted", 0},
                    {"emitted_total", std::uint64_t{}},
                    {"last_sent_tick", std::uint64_t{}},
                    {"overflow", 0},
                    {"sources", Json::array()}};
};
} // namespace forge::detail
