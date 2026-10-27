#pragma once
#include <forge/identity.hpp>
#include <stdexcept>
namespace forge {
enum class AudioSourceOperation : std::uint32_t { Stop, Restart, Pause, Resume, OneShot };
// Owner-thread commands; source identity includes its scene. No backend objects cross this API.
class AudioService {
  public:
    virtual ~AudioService() = default;
    virtual void play(EntityRef source) = 0;
    virtual void play_one_shot(EntityRef source) = 0;
    virtual void stop_source(EntityRef source) = 0;
    virtual void pause_source(EntityRef source) = 0;
    virtual void resume_source(EntityRef source) = 0;
    // Control-domain requests must target an admitted source that ignores game pause.
    virtual void control_unpaused_source(EntityRef, AudioSourceOperation) {
        throw std::runtime_error("Unpaused audio controls are unavailable");
    }
};
} // namespace forge
