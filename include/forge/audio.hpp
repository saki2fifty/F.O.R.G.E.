#pragma once
#include <array>
#include <chrono>
#include <forge/audio_components.hpp>
#include <forge/audio_service.hpp>
#include <forge/world.hpp>
#include <span>
namespace forge {
enum class AudioOutput { Device, Offline, NullDeviceTest };
struct AudioConfig {
    std::filesystem::path project;
    AudioOutput output = AudioOutput::Device;
    bool required = false;
    float master_volume = 1;
    std::array<float, 3> channel_volumes{1, 1, 1};
};
EngineModule audio_module(AudioConfig);
class AudioRuntime : public AudioService {
  public:
    AudioRuntime(WorldContext&, AudioConfig);
    ~AudioRuntime() override;
    void shutdown() noexcept;
    void synchronize(std::uint64_t tick);
    void synchronize(std::uint64_t tick, float fixed_delta,
                     std::span<const std::uint64_t> discontinuities = {});
    void paused(bool value);
    void master_volume(float value);
    void mix_volumes(float master, const std::array<float, 3>& channels);
    void play(EntityRef) override;
    void stop_source(EntityRef) override;
    void pause_source(EntityRef) override;
    void resume_source(EntityRef) override;
    Json status() const;
    // Explicit preparation/tooling wait; synchronization never waits for IO.
    bool wait_for_sources(std::chrono::milliseconds timeout);
    // Host service boundary, including paused Play; does not advance game time.
    void poll_loading();
    // Explicit no-device test facility; never called by the simulation clock.
    void read_offline(std::span<float> stereo);

  private:
    friend struct AudioPreviewAccess;
    friend struct AudioSourceAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
