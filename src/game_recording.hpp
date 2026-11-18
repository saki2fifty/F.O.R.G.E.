#pragma once
#include "avi_movie.hpp"
#include <forge/runtime.hpp>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace forge {
// Media progress for explicit offline recording. The host still owns GameSession,
// its RuntimeClock and AudioRuntime. No world or native state is retained here.
class GameRecording {
  public:
    using Mix = std::function<void(std::span<float>)>;
    using Advance = std::function<void(RuntimeClock::Time)>;
    using Ready = std::function<bool()>;
    static constexpr std::size_t audio_byte_limit = asset_detail::avi_game_audio_byte_limit;
    static constexpr unsigned audio_quantum = 128;
    static constexpr unsigned poll_quantum_limit = 16;
    static constexpr double minimum_elapsed = double((std::uint64_t(audio_quantum) * 1000000000 +
                                                      asset_detail::movie_audio_sample_rate - 1) /
                                                     asset_detail::movie_audio_sample_rate) /
                                              1000000000.;
    GameRecording(double fps, unsigned frames);
    std::vector<std::byte> read_frame(const Mix&, const Advance&);
    // One bounded portion of a frame. A missing result retains copied PCM only;
    // callers continue servicing their existing Scene/resource admission owners.
    std::optional<std::vector<std::byte>> poll_frame(const Mix&, const Advance&, const Ready&);
    RuntimeClock::Time time() const;
    Json status() const;
    unsigned completed() const { return completed_; }
    unsigned planned() const { return planned_; }
    std::uint64_t audio_frames() const { return samples_; }
    bool failed() const { return failed_ || busy_; }
    asset_detail::MovieFrameRate rate() const { return rate_; }

  private:
    asset_detail::MovieFrameRate rate_;
    unsigned planned_ = 0, completed_ = 0;
    std::uint64_t samples_ = 0;
    bool failed_ = false;
    bool busy_ = false, frame_pending_ = false;
    std::vector<std::byte> frame_pcm_;
    static RuntimeClock::Time at_sample(std::uint64_t);
};
} // namespace forge
