#pragma once
#include <forge/assets.hpp>
#include <forge/audio.hpp>
#include <memory>
namespace forge {
// Editor-only consumer of the normal audio provider. Owns no authored document.
class AudioClipPreview {
  public:
    AudioClipPreview();
    ~AudioClipPreview();
    void start(const std::filesystem::path&, const AssetRecord&,
               AudioOutput output = AudioOutput::Device);
    void stop();
    void configure(bool loop, float volume);
    void poll();
    bool active() const { return bool(world_); }
    bool matches(const std::filesystem::path&, const AssetRecord&) const;
    // Explicit internal offline-output consumer; never a device fallback.
    void read_offline(std::span<float>);

  private:
    struct PreviewWorld;
    std::unique_ptr<PreviewWorld> world_;
    bool loop_ = false;
    float volume_ = .5f;
};
} // namespace forge
