#pragma once
#include <atomic>
#include <cstdint>
#include <miniaudio.h>
#include <vector>

namespace forge::asset_detail {
// One existing mixer callback produces; its owner thread drains copied samples.
// The producer must be joined before this buffer is destroyed.
class AudioCaptureBuffer {
  public:
    static constexpr unsigned sample_rate = 48000, channels = 2;
    static constexpr unsigned maximum_queued_frames = 2 * sample_rate;
    static constexpr unsigned maximum_read_frames = 4096;
    enum class Failure : unsigned { None, Overflow, Nonfinite, Queue };
    struct Progress {
        bool active = false;
        Failure failure = Failure::None;
        std::uint64_t generation = 0, captured_frames = 0, read_frames = 0;
        unsigned queued_frames = 0;
    };
    explicit AudioCaptureBuffer(unsigned capacity = maximum_queued_frames);
    ~AudioCaptureBuffer();
    AudioCaptureBuffer(const AudioCaptureBuffer&) = delete;
    AudioCaptureBuffer& operator=(const AudioCaptureBuffer&) = delete;
    void start();
    void stop();
    // Device shutdown already joins the producer; retain its final copied prefix.
    void disable() noexcept;
    std::vector<float> read(unsigned maximum_frames);
    Progress progress() const;
    void observe(const float* stereo, std::uint64_t frames) noexcept;

  private:
    ma_pcm_rb queue_{};
    unsigned capacity_;
    bool initialized_ = false;
    std::uint64_t generation_ = 0, read_frames_ = 0;
    std::atomic<bool> active_{false};
    std::atomic<unsigned> callbacks_{0};
    std::atomic<Failure> failure_{Failure::None};
    void fail(Failure) noexcept;
};
} // namespace forge::asset_detail
