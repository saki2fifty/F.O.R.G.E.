#pragma once
#include "audio_stream_file.hpp"
#include <miniaudio.h>

namespace forge::asset_detail {
struct AudioStreamSlots {
    std::atomic<unsigned> reserved{0}, retiring{0};
    std::atomic<std::uint64_t> page_budget_bytes{0};
};
class AudioStreamSource {
  public:
    struct Retire {
        AudioRetirement::Sink sink;
        void operator()(AudioStreamSource*) const noexcept;
    };
    using Pointer = std::unique_ptr<AudioStreamSource, Retire>;
    // Empty means native slots are still retiring. A stable active limit throws.
    static Pointer create(ma_resource_manager&, std::shared_ptr<const AudioStreamFile>,
                          std::shared_ptr<AudioStreamSlots>, AudioRetirement::Sink, bool loop,
                          std::uint64_t initial_frame = 0);
    ~AudioStreamSource();
    AudioStreamSource(const AudioStreamSource&) = delete;
    AudioStreamSource& operator=(const AudioStreamSource&) = delete;
    bool ready();
    ma_data_source* data_source();
    // Offline/exclusive consumer only. No concurrent mutable mixer cursor read.
    std::uint64_t available_frames() const;
    std::uint64_t cursor() const;
    std::uint64_t page_bytes() const;
    AudioClipInfo info() const;
    unsigned underruns() const noexcept;
    ma_result read_error() const noexcept;

  private:
    AudioStreamSource() = default;
    ma_resource_manager_data_source source_{};
    struct ObservedSource {
        ma_data_source_base base{};
        AudioStreamSource* owner = nullptr;
    } observed_;
    std::atomic<unsigned> underruns_{0};
    std::atomic<ma_result> read_error_{MA_SUCCESS};
    std::shared_ptr<const AudioStreamFile> file_;
    std::shared_ptr<AudioStreamSlots> slots_;
    std::filesystem::path native_path_;
    std::string utf8_path_;
    bool initialized_ = false, prepared_ = false, retiring_ = false;
};
} // namespace forge::asset_detail
