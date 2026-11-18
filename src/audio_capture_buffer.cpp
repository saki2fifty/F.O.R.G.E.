#include "audio_capture_buffer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace forge::asset_detail {
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<AudioCaptureBuffer::Failure>::is_always_lock_free);

AudioCaptureBuffer::AudioCaptureBuffer(unsigned capacity) : capacity_(capacity) {
    if (!capacity || capacity > maximum_queued_frames)
        throw std::runtime_error("Live audio capture queue must contain1..96000 stereo frames");
}
AudioCaptureBuffer::~AudioCaptureBuffer() {
    if (initialized_)
        ma_pcm_rb_uninit(&queue_);
}
void AudioCaptureBuffer::disable() noexcept { active_.store(false); }
void AudioCaptureBuffer::stop() {
    disable();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (callbacks_.load() != 0) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Live audio capture callback did not retire");
        std::this_thread::yield();
    }
}
void AudioCaptureBuffer::start() {
    if (active_.load())
        throw std::runtime_error("Live audio capture is already active");
    stop();
    if (initialized_ && ma_pcm_rb_available_read(&queue_) != 0)
        throw std::runtime_error("Drain the previous live audio capture before restarting");
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Live audio capture generation exhausted");
    if (!initialized_) {
        if (ma_pcm_rb_init(ma_format_f32, channels, capacity_, nullptr, nullptr, &queue_) !=
            MA_SUCCESS)
            throw std::runtime_error("Live audio capture queue allocation failed");
        initialized_ = true;
    } else
        ma_pcm_rb_reset(&queue_);
    read_frames_ = 0;
    failure_.store(Failure::None);
    ++generation_;
    // Reset follows a quiescent disabled observation. A later callback can only
    // see enabled after initialization; it never inspects owner-only state.
    active_.store(true);
}
void AudioCaptureBuffer::fail(Failure value) noexcept {
    failure_.store(value);
    disable();
}
void AudioCaptureBuffer::observe(const float* stereo, std::uint64_t frames) noexcept {
    // Sequentially consistent entry/enable observations also protect restart:
    // an earlier enabled callback must retire before the owner resets storage.
    callbacks_.fetch_add(1);
    struct Exit {
        std::atomic<unsigned>& count;
        ~Exit() { count.fetch_sub(1); }
    } exit{callbacks_};
    if (!active_.load() || frames == 0)
        return;
    if (frames > capacity_ || frames > ma_pcm_rb_available_write(&queue_)) {
        fail(Failure::Overflow);
        return;
    }
    if (!stereo) {
        fail(Failure::Nonfinite);
        return;
    }
    for (std::uint64_t i = 0; i < frames * channels; ++i)
        if (!std::isfinite(stereo[i])) {
            fail(Failure::Nonfinite);
            return;
        }
    unsigned offset = 0;
    while (offset < frames) {
        auto count = unsigned(frames) - offset;
        void* destination = nullptr;
        if (ma_pcm_rb_acquire_write(&queue_, &count, &destination) != MA_SUCCESS || !count) {
            fail(Failure::Queue);
            return;
        }
        std::copy_n(stereo + std::size_t(offset) * channels, std::size_t(count) * channels,
                    static_cast<float*>(destination));
        if (ma_pcm_rb_commit_write(&queue_, count) != MA_SUCCESS) {
            fail(Failure::Queue);
            return;
        }
        offset += count;
    }
}
std::vector<float> AudioCaptureBuffer::read(unsigned maximum_frames) {
    if (!maximum_frames || maximum_frames > maximum_read_frames)
        throw std::runtime_error("Live audio reads require1..4096 stereo frames");
    if (!initialized_)
        throw std::runtime_error("Live audio capture has not started");
    const auto frames = std::min(maximum_frames, ma_pcm_rb_available_read(&queue_));
    std::vector<float> result(std::size_t(frames) * channels);
    unsigned offset = 0;
    while (offset < frames) {
        auto count = frames - offset;
        void* source = nullptr;
        if (ma_pcm_rb_acquire_read(&queue_, &count, &source) != MA_SUCCESS || !count)
            throw std::runtime_error("Live audio capture queue read failed");
        std::copy_n(static_cast<const float*>(source), std::size_t(count) * channels,
                    result.data() + std::size_t(offset) * channels);
        if (ma_pcm_rb_commit_read(&queue_, count) != MA_SUCCESS)
            throw std::runtime_error("Live audio capture queue consumption failed");
        read_frames_ += count;
        offset += count;
    }
    return result;
}
AudioCaptureBuffer::Progress AudioCaptureBuffer::progress() const {
    // This thread owns the read cursor. The native queue supplies committed
    // pending frames, so progress needs no separate callback sample counter.
    const auto queued =
        initialized_ ? ma_pcm_rb_available_read(const_cast<ma_pcm_rb*>(&queue_)) : 0;
    return {active_.load(),        failure_.load(), generation_,
            read_frames_ + queued, read_frames_,    queued};
}
} // namespace forge::asset_detail
