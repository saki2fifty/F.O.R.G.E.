#pragma once
#include "audio_asset.hpp"
#include <array>
#include <forge/assets.hpp>
#include <forge/audio.hpp>
#include <memory>
#include <optional>
namespace forge {
namespace asset_detail {
// Editor envelope only; not another PCM cache or serialized audio format.
struct AudioWaveform {
    std::vector<std::array<float, 2>> peaks; // bucket-major, then channel: min/max
    std::uint64_t frames = 0;
    std::uint32_t channels = 0, rate = 0;
    std::size_t buckets() const { return channels ? peaks.size() / channels : 0; }
    double duration() const { return rate ? double(frames) / rate : 0; }
};
AudioWaveform audio_waveform(const AudioClipData&);
} // namespace asset_detail
// Editor-only consumer of the normal audio provider. Owns no authored document.
class AudioClipPreview {
  public:
    AudioClipPreview();
    ~AudioClipPreview();
    void start(const std::filesystem::path&, const AssetRecord&,
               AudioOutput output = AudioOutput::Device);
    void stop();
    void clear();
    void configure(bool loop, float volume);
    void paused(bool);
    bool paused() const;
    void seek(double seconds);
    double position() const;
    void load_waveform(const std::filesystem::path&, const AssetRecord&);
    const asset_detail::AudioWaveform* waveform() const;
    bool waveform_matches(const std::filesystem::path&, const AssetRecord&) const;
    void poll();
    bool active() const { return bool(world_); }
    bool pending() const;
    bool waveform_pending() const;
    bool matches(const std::filesystem::path&, const AssetRecord&) const;
    // Explicit internal offline-output consumer; never a device fallback.
    void read_offline(std::span<float>);

  private:
    struct PreviewWorld;
    struct WaveformJob;
    std::unique_ptr<PreviewWorld> world_;
    std::unique_ptr<WaveformJob> waveform_job_;
    bool loop_ = false;
    float volume_ = .5f;
    asset_detail::AudioWaveform waveform_;
    std::filesystem::path waveform_project_;
    std::optional<AssetRecord> waveform_record_;
};
} // namespace forge
