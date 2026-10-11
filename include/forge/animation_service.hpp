#pragma once
#include <forge/identity.hpp>
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
// Owner-thread broadcast observations, valid until the next fixed tick/restore/stop.
// Consumers resolve EntityRef before acting; observations own no live entity pointer.
class AnimationService {
  public:
    virtual ~AnimationService() = default;
    virtual std::span<const AnimationEvent> events() const = 0;
};
} // namespace forge
