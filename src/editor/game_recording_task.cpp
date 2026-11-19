#include "game_recording_task.hpp"
#include "../input_recording_files.hpp"
#include "event_time.hpp"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace forge::ui {
GameRecordingTask::~GameRecordingTask() {
    if (finished_)
        return;
    try {
        abort("Editor closed", false);
        if (live_ && live_->begin_submitted && play_.active() && play_.launch_id() == launch_ &&
            play_.session() == session_)
            play_.retire_live_audio();
        if (readback_used_ && images_.busy() && host_.abandon_readback)
            host_.abandon_readback();
        // Only detached file/worker jobs can block here. The presentation owner
        // retains GPU retirement; shutdown must never wait on a device ticket.
        images_.finish_pending_file(image_message_);
        observe_image();
        if (encoder_.valid()) {
            try {
                movie_ = encoder_.get();
            } catch (const std::exception& e) {
                if (terminal_ == "failed" && error_.empty())
                    error_ = e.what();
            }
        }
        if (file_.valid()) {
            file_.wait();
            complete_file();
        }
        if (owns_folder_ && !finished_) {
            write_receipt();
            file_.wait();
            complete_file();
        }
    } catch (...) {
        // Shutdown cannot promise successful disk IO; futures still retain only
        // detached bytes/paths. Live observer retirement stays with PlaySession.
    }
}
const char* GameRecordingTask::phase_name() const {
    static constexpr std::array names{"configure", "prepare", "launch", "begin",  "image",
                                      "advance",   "pcm",     "encode", "retire", "done"};
    return phase_ == Phase::Done ? terminal_.c_str() : names.at(std::size_t(phase_));
}
void GameRecordingTask::require_source() const {
    document_.check_ownership();
    if (document_.project() != project_ || document_.generation() != generation_ ||
        scene_.asset_id() != scene_id_ || scene_.revision() != revision_)
        throw std::runtime_error("The authored Scene or project changed during game recording");
    if (host_.unavailable)
        if (const auto reason = host_.unavailable(); !reason.empty())
            throw std::runtime_error(reason);
}
void GameRecordingTask::require_launch() {
    if (!play_.active() || play_.launch_id() != launch_ ||
        (live_ ? play_.recording_launch() : !play_.recording_launch()))
        throw std::runtime_error("The recording Play process stopped or was replaced: " +
                                 play_.status());
    if (!session_.empty() &&
        (play_.session() != session_ ||
         (request_.capture_scope == "scene" && play_.world_generation() != world_generation_)))
        throw std::runtime_error("The recording runtime session or world changed");
    const auto& status = live_ ? play_.live_audio_status() : play_.recording_status();
    if (request_.capture_scope == "session" && status.is_object() &&
        (!live_ || (live_->capture && status.at("generation") == live_->capture)) &&
        (live_ || status.value("started", false))) {
        remember_recording_progress(status);
        // Once the Game copy is submitted, its private pixels and receipt stay
        // with their original World while readback/file retirement finishes.
        if (phase_ == Phase::Image && (!live_ || live_->image_requested) &&
            (frame_generation_ != play_.world_generation() ||
             (live_ && frame_generation_ != status.at("active_generation"))))
            throw std::runtime_error("The recording world changed while its PNG was in flight");
    }
}
void GameRecordingTask::remember_recording_progress(const Json& status) {
    // PlaySession admits the append-only wire witnesses. This receipt copy must
    // also follow media replies during End/drain, after image work has retired.
    if (request_.capture_scope == "session" && visits_.size() != status.at("visits").size())
        visits_ = status.at("visits");
    observed_audio_frames_ =
        status.at(live_ ? "captured_frames" : "audio_frames").get<std::uint64_t>();
}
void GameRecordingTask::stop_play() noexcept {
    if (live_) {
        live_->end_requested = true;
        return;
    }
    if (!launch_.empty() && play_.launch_id() == launch_ && play_.active()) {
        try {
            host_.stop();
        } catch (...) {
            // Preserve retirement without stopping a replacement user session.
            if (play_.launch_id() == launch_)
                play_.stop();
        }
    }
    if (user_data_scoped_) {
        try {
            if (!play_.active() && play_.user_data_override() == path_utf8(folder_ / "UserData"))
                play_.set_user_data_override(previous_user_data_);
        } catch (...) {
            // Retiring one recording cannot replace another launch's policy.
        }
        user_data_scoped_ = false;
    }
}
void GameRecordingTask::start() {
    document_.check_ownership();
    if (document_.project() != project_ || scene_.asset_id() != scene_id_)
        throw std::runtime_error("Choose the current project and authored Scene before recording");
    if (const auto reason = unavailable(play_, request_.recording_mode); !reason.empty())
        throw std::runtime_error(reason);
    revision_ = scene_.revision();
    generation_ = document_.generation();
    require_source();
    rate_ = request_.rate();
    if (request_.recording_mode == "live") {
        if (request_.capture_scope == "session" && !play_.live_audio_session_supported())
            throw std::runtime_error("Current Play does not support live recording across Scenes");
        live_.emplace();
        launch_ = play_.launch_id();
        session_ = play_.session();
        world_generation_ = play_.world_generation();
        frame_scene_id_ = play_.snapshot().at("asset_id").get<AssetId>();
    }
    if (!request_.input_recording.empty()) {
        replay_input_ =
            read_input_recording(project_, std::filesystem::u8path(request_.input_recording));
        if (replay_input_.contains("segments") && request_.capture_scope == "scene")
            throw std::runtime_error(
                "Game movies currently require a single-Scene input recording");
        if (!replay_input_.contains("segments") && request_.capture_scope == "session")
            throw std::runtime_error("Across-Scene movies require a session input recording");
    }
    started_at_ = event_time_local();
    folder_ = std::filesystem::absolute(request_.destination).lexically_normal();
    asset_storage::ordinary(folder_);
    if (folder_.filename().empty() || !std::filesystem::is_directory(folder_.parent_path()) ||
        std::filesystem::exists(folder_))
        throw std::runtime_error("Choose a new recording folder with an existing parent");
    const auto source = scene_.snapshot().dump();
    source_digest_ =
        asset_detail::content_digest(std::as_bytes(std::span(source.data(), source.size())));
    const auto destination = folder_;
    file_ = std::async(std::launch::async, [destination] {
        asset_storage::ordinary(destination);
        if (!std::filesystem::create_directory(asset_detail::native_io_path(destination)))
            throw std::runtime_error("Recording destination already exists");
        return std::size_t(0);
    });
    file_job_ = FileJob::Prepare;
    phase_ = Phase::Prepare;
    status_ = live_ ? "Preparing live game recording" : "Preparing fresh game recording";
}
void GameRecordingTask::abort(std::string reason, bool failed) {
    if (finished_)
        return;
    if (failed) {
        error_ = reason;
        terminal_ = "failed";
        report_error("game-recording", reason);
    } else if (terminal_.empty())
        terminal_ = "cancelled";
    reason_ = std::move(reason);
    status_ = "Retiring game recording: " + reason_;
    retiring_ = true;
    encoder_stop_.request_stop();
    stop_play();
    (void)images_.cancel(image_message_);
}
void GameRecordingTask::observe_image() {
    const auto index = live_ ? captured_ : completed_;
    if (images_.busy() || frame_image_done_ || folder_.empty() ||
        index >= unsigned(request_.frames))
        return;
    const auto expected = folder_ / request_.plan().filename(index);
    if (images_.completed_image() == expected && images_.error().empty()) {
        frame_image_done_ = true;
        ++captured_;
        asset_storage::ordinary(expected);
        const auto size = std::filesystem::file_size(asset_detail::native_io_path(expected));
        if (size > asset_detail::sequence_snapshot_byte_limit - png_bytes_ ||
            audio_frames_ * 4 > asset_detail::sequence_snapshot_byte_limit - png_bytes_ - size)
            throw std::runtime_error("Recording PNG and PCM snapshots exceed 256 MiB");
        png_bytes_ += std::size_t(size);
        if (live_) {
            live_->image_requested = false;
            complete_live_image(std::size_t(size));
        }
    } else if (!images_.error().empty()) {
        frame_image_done_ = true;
        if (!retiring_)
            throw std::runtime_error(images_.error());
    }
}
void GameRecordingTask::complete_file() {
    const auto job = file_job_;
    const auto bytes = file_.get();
    file_job_ = FileJob::None;
    if (job == FileJob::Prepare) {
        owns_folder_ = true;
        if (!retiring_) {
            require_source();
            if (live_) {
                require_launch();
                phase_ = Phase::Launch;
                status_ = "Preparing live Game output";
                deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
                return;
            }
            try {
                user_data_scoped_ = true;
                host_.start(folder_ / "UserData");
            } catch (...) {
                if (play_.active() && play_.recording_launch())
                    launch_ = play_.launch_id();
                throw;
            }
            launch_ = play_.launch_id();
            require_launch();
            phase_ = Phase::Launch;
            status_ = "Preparing game resources at tick zero";
            deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        }
    } else if (job == FileJob::Pcm) {
        audio_frames_ += bytes / 4;
        if (live_)
            return;
        ++completed_;
        if (!retiring_) {
            if (completed_ == unsigned(request_.frames))
                encode();
            else
                begin_frame();
        }
    } else if (job == FileJob::Normalize) {
        live_->normalized = true;
        audio_frames_ = bytes / 4;
        if (!retiring_)
            encode();
    } else if (job == FileJob::Receipt) {
        finished_ = true;
        phase_ = Phase::Done;
        status_ = terminal_ == "complete"
                      ? "Game movie, PNG frames and PCM saved: " + path_utf8(folder_)
                  : terminal_ == "cancelled" ? "Game recording cancelled; completed files retained"
                                             : "Game recording failed: " + error_;
    }
}
void GameRecordingTask::begin_frame() {
    require_source();
    require_launch();
    frame_scene_id_ = play_.snapshot().at("asset_id").get<AssetId>();
    frame_generation_ = play_.world_generation();
    frame_image_done_ = false;
    stable_ = 0;
    const auto filename = request_.plan().filename(live_ ? captured_ : completed_);
    const auto path = folder_ / filename;
    asset_storage::ordinary(path);
    if (std::filesystem::exists(path))
        throw std::runtime_error("A recording frame destination was created by another writer");
    auto& rows = live_ ? live_->images : frame_rows_;
    rows.push_back({{"file", filename},
                    {"time_seconds", double(audio_frames_) / asset_detail::movie_audio_sample_rate},
                    {"audio_start_frame", audio_frames_},
                    {"presentation_seconds", play_.timing().at("presentation_seconds")},
                    {"simulation_tick", play_.timing().at("tick")},
                    {"alpha", play_.timing().at("alpha")}});
    if (request_.capture_scope == "session") {
        rows.back()["scene"] = frame_scene_id_;
        rows.back()["world_generation"] = frame_generation_;
        rows.back()["source"] = visits_.back().at("source");
    }
    if (live_) {
        live_->image_requested = true;
        if (host_.pixels)
            sample_live_frame();
    }
    const auto accepted =
        host_.pixels ? images_.selected_path(ViewImageTask::View::Texture, path, project_, {},
                                             image_message_, ViewImageFormat::Png,
                                             host_.pixels(live_ ? captured_ : completed_))
                     : images_.selected_path(ViewImageTask::View::Game, path, project_,
                                             frame_scene_id_, image_message_, ViewImageFormat::Png);
    if (!accepted)
        throw std::runtime_error("Recording PNG request failed: " + images_.status());
    if (live_ && host_.pixels)
        live_->image_requested = false;
    phase_ = Phase::Image;
    status_ = "Recording game frame " + std::to_string(completed_ + 1) + " / " +
              std::to_string(request_.frames);
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
}
void GameRecordingTask::write_pcm(RecordedAudioFrame audio) {
    const auto [start, end] = recorded_audio_bounds(completed_, rate_);
    if (audio.frame != completed_ || audio.start_audio_frame != audio_frames_ ||
        start != audio_frames_ || audio.pcm.size() != (end - start) * 4 ||
        end * 4 > asset_detail::sequence_snapshot_byte_limit - png_bytes_)
        throw std::runtime_error(
            "Game audio receipt differs from its captured frame or byte budget");
    append_pcm(std::move(audio.pcm), audio_frames_, asset_detail::avi_game_audio_byte_limit);
    phase_ = Phase::Pcm;
    status_ = "Saving completed game audio interval";
}
void GameRecordingTask::append_pcm(std::vector<std::byte> pcm, std::uint64_t cursor,
                                   std::size_t limit) {
    const auto destination = folder_ / "audio.s16le";
    const auto expected = cursor * 4;
    file_ = std::async(std::launch::async, [destination, expected, limit, pcm = std::move(pcm)] {
        asset_storage::ordinary(destination);
        const auto path = asset_detail::native_io_path(destination);
        const auto size = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
        if (size != expected || size > limit || pcm.size() > limit - size)
            throw std::runtime_error("Recording PCM file changed before append");
        std::ofstream out;
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.open(path, std::ios::binary | std::ios::app);
        out.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(pcm.size()));
        out.close();
        return pcm.size();
    });
    file_job_ = FileJob::Pcm;
}
void GameRecordingTask::encode() {
    stop_play();
    std::vector<std::string> names;
    for (unsigned frame = 0; frame < completed_; ++frame)
        names.push_back(live_ ? frame_rows_.at(frame).at("file").get<std::string>()
                              : request_.plan().filename(frame));
    const auto request = request_;
    const auto project = project_, folder = folder_, worker = worker_;
    const auto stop = encoder_stop_.get_token();
    encoder_ = std::async(
        std::launch::async, [request, project, folder, worker, names = std::move(names), stop] {
            return asset_detail::write_avi_game(
                worker, project, folder, names, unsigned(request.output.width),
                unsigned(request.output.height), request.fps, request.output.jpeg_quality, stop);
        });
    phase_ = Phase::Encode;
    status_ = "Encoding synchronized game movie";
}
Json GameRecordingTask::movie_document() const {
    const auto& profile =
        asset_detail::sequence_encoding_profile(asset_detail::SequenceEncoding::AviGame);
    return {{"file", path_utf8(folder_ / profile.filename)},
            {"mime", profile.mime},
            {"profile", profile.profile},
            {"codec", "mjpeg"},
            {"bytes", movie_->bytes},
            {"frames", completed_},
            {"rate", movie_->frame_rate.rate},
            {"scale", movie_->frame_rate.scale},
            {"jpeg_quality", request_.output.jpeg_quality},
            {"audio", true},
            {"audio_format", "pcm_s16le"},
            {"audio_frames", movie_->audio_frames},
            {"sample_rate", asset_detail::movie_audio_sample_rate},
            {"channels", 2},
            {"component_bits", 16}};
}
Json GameRecordingTask::receipt() const {
    auto frames = Json::array();
    for (unsigned i = 0; i < (live_ ? completed_media_frames() : captured_); ++i)
        frames.push_back(frame_rows_.at(i));
    Json value{{"format", "forge.game-recording"},
               {"version", live_ ? (request_.capture_scope == "session" ? 4 : 3)
                           : request_.capture_scope == "session" ? 2
                                                                 : 1},
               {"mode", live_ ? "live_game" : "offline_game"},
               {"state", terminal_},
               {"error", error_},
               {"reason", reason_},
               {"started_at", started_at_},
               {"finished_at", event_time_local()},
               {"engine_source", source_commit},
               {"build_id", build_id},
               {"scene", scene_id_},
               {"scene_revision", revision_},
               {"source_digest", source_digest_},
               {"launch", launch_},
               {"session", session_},
               {"world_generation", world_generation_},
               {"planned_frames", request_.frames},
               {"completed_frames", completed_media_frames()},
               {"captured_png_frames", captured_},
               {"width", request_.output.width},
               {"height", request_.output.height},
               {"fps", request_.fps},
               {"rate", rate_.rate},
               {"scale", rate_.scale},
               {"pcm_file", "audio.s16le"},
               {"audio_frames", audio_frames_},
               {"sample_rate", asset_detail::movie_audio_sample_rate},
               {"channels", 2},
               {"component_bits", 16},
               {"frames", std::move(frames)}};
    if (request_.capture_scope == "session") {
        value["capture_scope"] = "session";
        value["profile"] = "offline-session-game-v2";
        value["visits"] = visits_;
        value["observed_audio_frames"] = observed_audio_frames_;
    }
    if (live_) {
        value["recording_mode"] = "live";
        value["capture_scope"] = request_.capture_scope;
        value["profile"] =
            request_.capture_scope == "session" ? "live-session-game-v4" : "live-scene-game-v3";
        if (request_.capture_scope == "session") {
            value["source_visits"] = visits_;
            // Wire boundaries use the original capture cursor. The movie drops
            // its warm-up prefix, so retain both coordinate systems explicitly.
            for (auto& visit : value["visits"]) {
                const auto source = visit.at("audio_frame").get<std::uint64_t>();
                visit["source_audio_frame"] = source;
                visit["audio_frame"] =
                    live_->origin ? Json(source > *live_->origin ? source - *live_->origin : 0)
                                  : Json();
            }
        }
        value["capture_scene"] = frame_scene_id_;
        value["observed_audio_frames"] = observed_audio_frames_;
        value["audio_source_start_frame"] = live_->origin ? Json(*live_->origin) : Json();
        value["audio_normalized"] = live_->normalized;
        value["raw_audio_frames"] = live_->normalized ? live_->raw_audio_frames : audio_frames_;
        value["images"] = Json::array();
        for (unsigned i = 0; i < captured_; ++i)
            value["images"].push_back(live_->images.at(i));
    }
    if (!replay_input_.is_null()) {
        const auto text = replay_input_.dump();
        std::size_t ticks = 0;
        if (replay_input_.contains("segments"))
            for (const auto& segment : replay_input_.at("segments"))
                ticks += segment.at("frames").size();
        else
            ticks = replay_input_.at("frames").size();
        value["input_recording"] = {
            {"admitted", replay_admitted_},
            {"file", "input-recording.json"},
            {"source", replay_input_.at("source")},
            {"ticks", ticks},
            {"digest", asset_detail::content_digest(std::as_bytes(std::span(text)))}};
    }
    if (movie_) {
        value["movie"] = movie_document();
        value["movie"]["file"] = "game.avi";
    }
    return value;
}
void GameRecordingTask::retire() {
    stop_play();
    if (live_ && !service_live_audio())
        return;
    if (images_.busy() || encoder_.valid() || file_.valid())
        return;
    if (!owns_folder_) {
        finished_ = true;
        phase_ = Phase::Done;
        status_ = reason_;
        return;
    }
    write_receipt();
}
void GameRecordingTask::write_receipt() {
    if (live_ && !live_->normalized && live_->origin && audio_frames_ >= *live_->origin)
        fill_live_frames(asset_detail::avi_game_frames_at_audio(rate_, unsigned(request_.frames),
                                                                audio_frames_ - *live_->origin));
    const auto destination = folder_ / "recording.json";
    auto text = receipt().dump(2);
    auto input = replay_input_.is_null() ? std::string{} : replay_input_.dump();
    file_ = std::async(
        std::launch::async, [destination, text = std::move(text), input = std::move(input)] {
            if (!input.empty())
                asset_storage::replace(destination.parent_path() / "input-recording.json", input);
            asset_storage::replace(destination, text);
            return std::size_t(0);
        });
    file_job_ = FileJob::Receipt;
    phase_ = Phase::Retire;
    status_ = "Saving game recording receipt";
}
void GameRecordingTask::draw() {
    if (!show_)
        return;
    const auto* viewport = ImGui::GetMainViewport();
    const auto scale = ImGui::GetFontSize() / 13.f;
    ImGui::SetNextWindowSizeConstraints({std::min(360.f * scale, viewport->WorkSize.x - 32), 0},
                                        {viewport->WorkSize.x - 32, viewport->WorkSize.y - 32});
    if (!ImGui::Begin("Record game", &show_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        if (!show_ && !finished_)
            (void)cancel();
        return;
    }
    ImGui::TextWrapped(request_.recording_mode == "live"
                           ? "Record the current Play session with live input and sound. The "
                             "Across scenes option follows game-requested Scene changes."
                           : "Fresh offline game run with synchronized audio. Game logic and "
                             "physics run at the project's fixed rate.");
    if (phase_ == Phase::Configure && !retiring_) {
        bool live = request_.recording_mode == "live";
        if (ImGui::Checkbox("Record current Play", &live)) {
            request_.recording_mode = live ? "live" : "offline";
            if (live) {
                request_.input_recording.clear();
            }
        }
        FORGE_UI_PROBE("game-recording:mode");
        if (auto result = dialog_.take()) {
            if (!result->error.empty())
                validation_ = result->error;
            else if (!result->path.empty() && result->path.size() < parent_.size()) {
                parent_.fill(0);
                std::copy(result->path.begin(), result->path.end(), parent_.begin());
            }
        }
        ImGui::TextUnformatted("Parent folder");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##game-recording-parent", parent_.data(), parent_.size());
        FORGE_UI_PROBE("game-recording:parent");
        if (ImGui::Button("Browse..."))
            dialog_.show(FileDialog::Kind::SequenceFramesParent, host_.window, parent_.data());
        ImGui::TextUnformatted("New recording folder");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##game-recording-name", name_.data(), name_.size());
        FORGE_UI_PROBE("game-recording:name");
        ImGui::TextUnformatted("Frames per second");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputDouble("##game-recording-fps", &request_.fps, 1, 10, "%.6g");
        FORGE_UI_PROBE("game-recording:fps");
        ImGui::TextUnformatted("Frame count");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##game-recording-frames", &request_.frames);
        FORGE_UI_PROBE("game-recording:frames");
        ImGui::TextUnformatted("Width (pixels)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##game-recording-width", &request_.output.width);
        FORGE_UI_PROBE("game-recording:width");
        ImGui::TextUnformatted("Height (pixels)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##game-recording-height", &request_.output.height);
        FORGE_UI_PROBE("game-recording:height");
        ImGui::TextUnformatted("JPEG quality");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##game-recording-quality", &request_.output.jpeg_quality,
                         FORGE_JPEG_QUALITY_MIN, FORGE_JPEG_QUALITY_MAX);
        FORGE_UI_PROBE("game-recording:quality");
        ImGui::BeginDisabled(live);
        ImGui::TextUnformatted("Input recording (optional)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##game-recording-input", &request_.input_recording);
        FORGE_UI_PROBE("game-recording:input");
        ImGui::TextWrapped(
            "Use Recordings/<name>.json from this project to replay saved fixed gameplay actions.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(live && !play_.live_audio_session_supported());
        bool across = request_.capture_scope == "session";
        if (ImGui::Checkbox("Across scenes", &across))
            request_.capture_scope = across ? "session" : "scene";
        FORGE_UI_PROBE("game-recording:scope");
        ImGui::EndDisabled();
        bool valid = false;
        try {
            (void)request_.rate();
            if (host_.unavailable)
                if (const auto reason = host_.unavailable(); !reason.empty())
                    throw std::runtime_error(reason);
            if (const auto reason = unavailable(play_, request_.recording_mode); !reason.empty())
                throw std::runtime_error(reason);
            if (live && across && !play_.live_audio_session_supported())
                throw std::runtime_error("Current Play does not support recording across Scenes");
            const auto name = std::filesystem::u8path(name_.data());
            if (name.empty() || name.has_parent_path() || name == "." || name == ".." ||
                !parent_[0])
                throw std::runtime_error("Choose an existing parent and one new folder name");
            request_.destination = std::filesystem::u8path(parent_.data()) / name;
            if (!std::filesystem::is_directory(request_.destination.parent_path()) ||
                std::filesystem::exists(request_.destination))
                throw std::runtime_error("Choose a new recording folder with an existing parent");
            validation_.clear();
            valid = !dialog_.busy();
        } catch (const std::exception& e) {
            validation_ = e.what();
        }
        if (!validation_.empty())
            ImGui::TextWrapped("%s", validation_.c_str());
        ImGui::TextWrapped(
            live
                ? "Includes actual PNG captures, stereo 48 kHz PCM16, a movie and timing metadata. "
                  "Missed video intervals repeat the last saved image. Across scenes follows up "
                  "to 64 Scene visits; otherwise a Scene change stops the clip. Play and its "
                  "user data stay in use."
                : "Includes PNG frames, stereo 48 kHz PCM16, a movie and a recording receipt. "
                  "This run uses fresh user data. Across scenes admits up to 64 game-requested "
                  "Scene visits; optional input must be an Across Scenes take.");
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Start recording"))
            try {
                start();
            } catch (const std::exception& e) {
                abort(e.what(), true);
            }
        FORGE_UI_PROBE("game-recording:start");
        ImGui::EndDisabled();
    } else {
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::Text("Completed frames: %u / %d", completed_, request_.frames);
        if (request_.frames > 0)
            ImGui::ProgressBar(float(completed_) / float(request_.frames), {-1, 0});
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        if (!folder_.empty()) {
            ImGui::TextWrapped("%s", path_utf8(folder_).c_str());
            if (ImGui::Button("Copy output folder"))
                ImGui::SetClipboardText(path_utf8(folder_).c_str());
        }
    }
    if (ImGui::Button(finished_ ? "Close" : "Cancel")) {
        if (!finished_)
            (void)cancel();
        show_ = false;
    }
    FORGE_UI_PROBE("game-recording:cancel");
    ImGui::End();
    if (!show_ && !finished_)
        (void)cancel();
}
} // namespace forge::ui
