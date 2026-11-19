#include "event_time.hpp"
#include "game_recording_task.hpp"

namespace forge::ui {
std::string GameRecordingTask::unavailable(const PlaySession& play, const std::string& mode) {
    if (mode == "offline")
        return play.active() ? "Stop Play before starting a fresh offline recording" : "";
    if (mode != "live")
        return "Recording mode must be offline or live";
    if (!play.control_ready() || !play.live_audio_supported())
        return "Start ordinary SDK Play before recording live Game output";
    const auto& audio = play.audio_status();
    if (!audio.is_object() ||
        (audio.value("output", "") != "device" && audio.value("output", "") != "null_test"))
        return "Live recording requires Play's existing audio device";
    const auto& capture = play.live_audio_status();
    if (play.live_audio_retiring() || play.live_audio_pending() ||
        (capture.is_object() &&
         (capture.at("state") == "recording" || capture.at("queued_frames") != 0)))
        return "Finish the current live audio capture before starting another";
    return {};
}
void GameRecordingTask::sample_live_frame() {
    require_launch();
    if (!live_world_ready())
        throw std::runtime_error(
            "The live recording World changed before its Game image was copied");
    const auto observed = play_.live_audio_status().at("captured_frames").get<std::uint64_t>();
    if (!live_->origin) {
        if (observed > 2 * asset_detail::movie_audio_sample_rate)
            throw std::runtime_error("Live Game output was unavailable during audio warm-up");
        live_->origin = observed;
    }
    auto& row = live_->images.back();
    row["image"] = captured_;
    row["scene"] = frame_scene_id_;
    row["world_generation"] = frame_generation_;
    row["simulation_tick"] = play_.timing().at("tick");
    row["presentation_seconds"] = play_.timing().at("presentation_seconds");
    row["alpha"] = play_.timing().at("alpha");
    row["observed_audio_frame"] = observed;
    row["source_audio_frame"] = observed - *live_->origin;
    row["audio_start_frame"] = observed - *live_->origin;
    row["time_seconds"] = double(observed - *live_->origin) / asset_detail::movie_audio_sample_rate;
    row["captured_at"] = event_time_local();
}
bool GameRecordingTask::live_world_ready() const {
    const auto& progress = play_.live_audio_status();
    if (!progress.is_object() || progress.at("active_generation") != play_.world_generation())
        return false;
    if (request_.capture_scope == "scene")
        return true;
    const auto& visits = progress.at("visits");
    return !visits.empty() && visits.back().at("generation") == play_.world_generation() &&
           visits.back().at("scene") == play_.snapshot().at("asset_id");
}
void GameRecordingTask::fill_live_frames(unsigned until) {
    if (live_->last_image.is_null())
        return;
    until = std::min(until, unsigned(request_.frames));
    const auto audio_bytes =
        asset_detail::avi_movie_audio_frames(rate_, unsigned(request_.frames)) * 4;
    while (completed_ < until) {
        if (live_->last_png_bytes >
                asset_detail::sequence_snapshot_byte_limit - live_->movie_png_bytes ||
            audio_bytes > asset_detail::sequence_snapshot_byte_limit - live_->movie_png_bytes -
                              live_->last_png_bytes)
            throw std::runtime_error("Live movie frame references and PCM exceed256MiB");
        auto row = live_->last_image;
        row["frame"] = completed_;
        row["audio_start_frame"] = asset_detail::avi_movie_audio_frames(rate_, completed_);
        row["time_seconds"] = double(row.at("audio_start_frame").get<std::uint64_t>()) /
                              asset_detail::movie_audio_sample_rate;
        row["repeated"] = !frame_rows_.empty() && frame_rows_.back().at("image") == row.at("image");
        frame_rows_.push_back(std::move(row));
        live_->movie_png_bytes += live_->last_png_bytes;
        ++completed_;
    }
}
void GameRecordingTask::complete_live_image(std::size_t bytes) {
    const auto& row = live_->images.at(captured_ - 1);
    const auto source = row.at("source_audio_frame").get<std::uint64_t>();
    auto first = asset_detail::avi_game_frames_at_audio(rate_, unsigned(request_.frames), source);
    if (first < unsigned(request_.frames) &&
        asset_detail::avi_movie_audio_frames(rate_, first) < source)
        ++first;
    fill_live_frames(first);
    live_->last_image = row;
    live_->last_png_bytes = bytes;
}
bool GameRecordingTask::service_live_audio() {
    if (live_->normalized || !live_->begin_submitted || !play_.active() ||
        play_.launch_id() != launch_ || play_.session() != session_)
        return true;
    if (!play_.live_audio_error().empty() && !retiring_)
        throw std::runtime_error(play_.live_audio_error());
    if (!live_->capture) {
        if (play_.live_audio_pending())
            return false;
        if (!play_.live_audio_error().empty())
            return true;
        if (!play_.live_audio_status().is_object())
            return false;
        live_->capture = play_.live_audio_status().at("generation").get<std::uint64_t>();
    }
    const auto& progress = play_.live_audio_status();
    if (!progress.is_object() || progress.at("generation") != live_->capture ||
        progress.at("world_generation") != world_generation_) {
        if (retiring_)
            return true;
        throw std::runtime_error("The live audio capture was replaced");
    }
    remember_recording_progress(progress);
    if (live_->end_requested && !live_->end_submitted && !play_.live_audio_pending() &&
        play_.live_audio_ready()) {
        if (!play_.submit_live_audio("end"))
            throw std::runtime_error("Live audio capture could not end");
        live_->end_submitted = true;
    }
    if (!file_.valid()) {
        if (auto chunk = play_.take_live_audio()) {
            if (!live_->audio_discard && chunk->start_audio_frame != audio_frames_) {
                live_->audio_discard = true;
                if (!retiring_)
                    throw std::runtime_error("Live recording PCM is no longer a contiguous prefix");
            }
            if (!live_->audio_discard && !chunk->pcm.empty()) {
                if (audio_frames_ * 4 > live_audio_byte_limit ||
                    chunk->pcm.size() > live_audio_byte_limit - audio_frames_ * 4 ||
                    chunk->pcm.size() > asset_detail::sequence_snapshot_byte_limit - png_bytes_ ||
                    audio_frames_ * 4 > asset_detail::sequence_snapshot_byte_limit - png_bytes_ -
                                            chunk->pcm.size()) {
                    live_->audio_discard = true;
                    throw std::runtime_error(
                        "Live recording audio or combined snapshots exceed their byte budget");
                }
                append_pcm(std::move(chunk->pcm), audio_frames_, live_audio_byte_limit);
            }
        }
    }
    if (!retiring_ && progress.at("state") == "failed")
        throw std::runtime_error(progress.at("error").get<std::string>());
    if (file_.valid() || play_.live_audio_pending())
        return false;
    if (progress.at("queued_frames") != 0 || !live_->end_requested) {
        if (play_.live_audio_ready() && !play_.submit_live_audio("read"))
            throw std::runtime_error("Live audio interval could not be queued");
        return false;
    }
    return live_->end_submitted && progress.at("state") != "recording";
}
void GameRecordingTask::poll_live() {
    if (phase_ == Phase::Prepare)
        return;
    require_launch();
    if (phase_ == Phase::Launch) {
        if ((live_->render_ready || host_.pixels) && play_.control_ready()) {
            if (play_.world_generation() != world_generation_)
                throw std::runtime_error("The live recording World changed before capture Begin");
            if (!play_.submit_live_audio("begin", 4096, request_.capture_scope))
                throw std::runtime_error("Live mixer recording could not begin");
            live_->begin_submitted = true;
            phase_ = Phase::Begin;
            status_ = "Attaching to the existing Game mixer";
        }
    } else if (phase_ == Phase::Begin) {
        if (!play_.live_audio_pending() && play_.live_audio_status().is_object()) {
            if (!play_.live_audio_error().empty())
                throw std::runtime_error(play_.live_audio_error());
            live_->capture = play_.live_audio_status().at("generation").get<std::uint64_t>();
            if (live_world_ready())
                begin_frame();
        }
    } else if (phase_ == Phase::Image) {
        const auto settled = service_live_audio();
        if (!live_->origin) {
            if (observed_audio_frames_ > 2 * asset_detail::movie_audio_sample_rate)
                throw std::runtime_error(
                    "Live recording needs ready Game output during audio warm-up");
        } else {
            const auto elapsed = observed_audio_frames_ - *live_->origin;
            const auto due =
                asset_detail::avi_game_frames_at_audio(rate_, unsigned(request_.frames), elapsed);
            if (due == unsigned(request_.frames))
                live_->end_requested = true;
            if (!images_.busy() && !file_.valid() && !live_->image_requested) {
                if (live_->end_requested && settled) {
                    fill_live_frames(unsigned(request_.frames));
                    normalize_live_audio();
                } else if (!live_->end_requested && due > live_->requested_frame &&
                           captured_ < unsigned(request_.frames) && live_world_ready()) {
                    live_->requested_frame = due;
                    begin_frame();
                }
            }
        }
    }
    if (std::chrono::steady_clock::now() >= deadline_)
        throw std::runtime_error(
            "Live recording needs a visible Game view and ready resources; capture timed out");
}
void GameRecordingTask::normalize_live_audio() {
    if (!live_->origin || live_->last_image.is_null() || completed_ != unsigned(request_.frames))
        throw std::runtime_error("Live recording has no complete image sequence");
    const auto samples = asset_detail::avi_movie_audio_frames(rate_, completed_);
    const auto origin = *live_->origin;
    if (origin > audio_frames_ || samples > audio_frames_ - origin)
        throw std::runtime_error("Live recording did not retain its complete audio interval");
    live_->raw_audio_frames = audio_frames_;
    const auto destination = folder_ / "audio.s16le";
    const auto expected = audio_frames_ * 4;
    file_ = std::async(std::launch::async, [destination, expected, origin, samples] {
        asset_storage::ordinary(destination);
        const auto path = asset_detail::native_io_path(destination);
        if (std::filesystem::file_size(path) != expected)
            throw std::runtime_error("Live audio file changed before movie normalization");
        std::ifstream input(path, std::ios::binary);
        input.exceptions(std::ios::badbit | std::ios::failbit);
        input.seekg(std::streamoff(origin * 4));
        std::string pcm(std::size_t(samples) * 4, '\0');
        input.read(pcm.data(), std::streamsize(pcm.size()));
        input.close();
        asset_storage::replace(destination, pcm);
        return pcm.size();
    });
    file_job_ = FileJob::Normalize;
    status_ = "Finalizing live movie audio";
}
} // namespace forge::ui
