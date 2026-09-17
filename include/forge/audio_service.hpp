#pragma once
#include <forge/identity.hpp>
namespace forge {
// Owner-thread commands; source identity includes its scene. No backend objects cross this API.
class AudioService {
  public:
    virtual ~AudioService() = default;
    virtual void play(EntityRef source) = 0;
    virtual void stop_source(EntityRef source) = 0;
};
} // namespace forge
