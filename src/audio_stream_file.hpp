#pragma once
#include "audio_asset.hpp"
#include "audio_retirement.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>

namespace forge::asset_detail {
inline constexpr unsigned max_audio_streams = 8;
inline constexpr unsigned max_audio_stream_backings = 64;
inline constexpr std::uint64_t max_audio_stream_backing_bytes = 256ull * 1024 * 1024;
// Accounting only. Immutable clip resources and their copied backing values own
// the files; this object is not another asset registry or loaded-resource owner.
struct AudioStreamBudget {
    std::filesystem::path root;
    std::atomic<std::uint64_t> bytes{0};
    std::atomic<unsigned> files{0};
    std::once_flag cleanup;
    std::atomic<bool> cleanup_pending{false};
    AudioRetirement::Sink retirement;
    explicit AudioStreamBudget(std::filesystem::path temporary_root, AudioRetirement::Sink = {});
};
class AudioStreamFile {
  public:
    ~AudioStreamFile();
    AudioStreamFile(const AudioStreamFile&) = delete;
    AudioStreamFile& operator=(const AudioStreamFile&) = delete;
    static std::shared_ptr<const AudioStreamFile> create(std::span<const std::byte> cooked_pcm,
                                                         std::shared_ptr<AudioStreamBudget>,
                                                         std::stop_token = {});
    const std::filesystem::path& path() const;
    AudioClipInfo info() const;
    std::uint64_t byte_size() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
    explicit AudioStreamFile(std::unique_ptr<State>);
};
} // namespace forge::asset_detail
