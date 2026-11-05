#include "audio_retirement.hpp"
#include "audio_stream_file.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>

namespace forge::asset_detail {
struct AudioRetirement::State {
    struct Job {
        void* value = nullptr;
        void (*destroy)(void*) noexcept = nullptr;
    };
    static constexpr unsigned capacity = max_audio_streams + max_audio_stream_backings;
    std::array<Job, capacity> jobs{};
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<unsigned> pending{0};
    unsigned begin = 0, count = 0;
    bool closing = false, closed = false;
};
AudioRetirement::Sink::Sink(std::shared_ptr<State> state) : state_(std::move(state)) {}
void AudioRetirement::Sink::retire(void* value, void (*destroy)(void*) noexcept) const noexcept {
    if (!value)
        return;
    if (!destroy)
        std::terminate();
    if (state_) {
        bool queued = false;
        {
            std::lock_guard lock(state_->mutex);
            if (!state_->closed && state_->count < State::capacity) {
                state_->jobs[(state_->begin + state_->count++) % State::capacity] = {value,
                                                                                     destroy};
                state_->pending.fetch_add(1, std::memory_order_relaxed);
                queued = true;
            }
        }
        if (queued) {
            state_->changed.notify_one();
            return;
        }
    }
    // A default/closed sink can outlive the provider through copied immutable
    // backing. Provider close has already stopped callbacks and joined its work.
    destroy(value);
}
unsigned AudioRetirement::Sink::pending() const noexcept {
    return state_ ? state_->pending.load(std::memory_order_relaxed) : 0;
}
AudioRetirement::AudioRetirement() : state_(std::make_shared<State>()) {
    worker_ = std::thread([state = state_] {
        for (;;) {
            State::Job job;
            {
                std::unique_lock lock(state->mutex);
                state->changed.wait(lock, [&] { return state->count || state->closing; });
                if (!state->count) {
                    state->closed = true;
                    return;
                }
                job = state->jobs[state->begin];
                state->begin = (state->begin + 1) % State::capacity;
                --state->count;
            }
            job.destroy(job.value);
            state->pending.fetch_sub(1, std::memory_order_relaxed);
        }
    });
}
AudioRetirement::~AudioRetirement() { close(); }
AudioRetirement::Sink AudioRetirement::sink() const { return Sink(state_); }
void AudioRetirement::close() {
    if (!worker_.joinable())
        return;
    {
        std::lock_guard lock(state_->mutex);
        state_->closing = true;
    }
    state_->changed.notify_one();
    worker_.join();
}
} // namespace forge::asset_detail
