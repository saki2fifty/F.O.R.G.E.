#pragma once
#include <cstddef>
#include <cstdint>
#include <forge/input.hpp>
#include <forge/scene.hpp>
#include <string>

namespace forge::detail {
// Transient fixed-tick projection. Flecs owns types, values and observer dispatch;
// Scene and RuntimeInput retain all authoring and input authority.
class InputMessageRuntime {
  public:
    InputMessageRuntime(WorldContext& context, Scene& scene) : context_(context), scene_(scene) {}
    void tick(const InputSnapshot&, const InputMap&);
    const Json& status() const { return status_; }

  private:
    static constexpr std::size_t preview_limit = 2048;
    WorldContext& context_;
    Scene& scene_;
    struct Observation {
        flecs::entity_t source{};
        ActionId action;
        ActionKind kind{};
        std::string context, payload;
        std::uint32_t trigger{};
        ActionState value;
        std::uint64_t emitted_total{}, last_sent_tick{};
        std::size_t encoded_bytes{};
        std::string preview;
        bool seen = false;
    };
    std::map<EntityId, Observation> observations_;
    Json status_ = {{"tick", 0},
                    {"emitted", 0},
                    {"emitted_total", std::uint64_t{}},
                    {"last_sent_tick", std::uint64_t{}},
                    {"preview_limit_bytes", preview_limit},
                    {"overflow", 0},
                    {"sources", Json::array()}};
};
} // namespace forge::detail
