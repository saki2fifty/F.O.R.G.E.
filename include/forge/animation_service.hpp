#pragma once
#include <array>
#include <forge/identity.hpp>
#include <optional>
#include <span>
#include <string>
namespace forge {
struct AnimationEvent {
    EntityRef entity;
    AssetId states, clip;
    std::uint32_t state = 0, marker = 0;
    std::uint64_t tick = 0;
    double offset = 0, phase = 0, value = 0;
    std::string name;
};
enum class AnimationControl : std::uint32_t { Play = 1, Pause = 2, Restart = 3 };
struct AnimationPlayback {
    EntityRef entity;
    AssetId states, clip, blend_clip;
    std::uint32_t state = 0;
    std::string state_name;
    double time = 0, duration = 0, progress = 0, speed = 0;
    double transition_weight = 1, blend_weight = 0;
    bool enabled = false, loop = false, playing = false, finished = false, transitioning = false;
};
struct AnimationRootMotion {
    EntityRef entity;
    AssetId joint, clip;
    std::uint64_t tick = 0;
    std::array<double, 3> translation{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    bool applied = false;
};
// Owner-thread broadcast observations, valid until the next fixed tick/restore/stop.
// Consumers resolve EntityRef before acting; observations own no live entity pointer.
class AnimationService {
  public:
    virtual ~AnimationService() = default;
    virtual std::span<const AnimationEvent> events() const = 0;
    // Realized incoming clip, derived from authoritative clocks without sampling.
    virtual std::optional<AnimationPlayback> playback(EntityRef) const = 0;
    // Model-holder local, before actor scale/rotation. Read after Animation in this tick.
    // Repeated observations do not consume or reapply the delta.
    virtual std::optional<AnimationRootMotion> root_motion(EntityRef) const = 0;
    // Before Animation in the current fixed tick; affects the realized player only.
    virtual bool control(EntityRef, AnimationControl, std::uint64_t current_tick) = 0;
    // Move the admitted incoming clock; skip markers/root displacement across the jump.
    virtual bool seek(EntityRef, double seconds, std::uint64_t current_tick) = 0;
};
} // namespace forge
