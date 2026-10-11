#pragma once
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
struct AnimationPlayback {
    EntityRef entity;
    AssetId states, clip, blend_clip;
    std::uint32_t state = 0;
    std::string state_name;
    double time = 0, duration = 0, progress = 0, speed = 0;
    double transition_weight = 1, blend_weight = 0;
    bool enabled = false, loop = false, playing = false, finished = false, transitioning = false;
};
// Owner-thread broadcast observations, valid until the next fixed tick/restore/stop.
// Consumers resolve EntityRef before acting; observations own no live entity pointer.
class AnimationService {
  public:
    virtual ~AnimationService() = default;
    virtual std::span<const AnimationEvent> events() const = 0;
    // Realized incoming clip, derived from authoritative clocks without sampling.
    virtual std::optional<AnimationPlayback> playback(EntityRef) const = 0;
};
} // namespace forge
