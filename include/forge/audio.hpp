#pragma once
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
};
EngineModule audio_module(AudioConfig);
class AudioRuntime : public AudioService {
  public:
    AudioRuntime(WorldContext&, AudioConfig);
    ~AudioRuntime() override;
    void shutdown() noexcept;
    void synchronize(std::uint64_t tick);
    void paused(bool value);
    void play(EntityRef) override;
    void stop_source(EntityRef) override;
    Json status() const;
    // Explicit no-device test facility; never called by the simulation clock.
    void read_offline(std::span<float> stereo);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
