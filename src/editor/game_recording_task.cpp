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
        // detached bytes/paths and the owned runtime has already been stopped.
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
void GameRecordingTask::require_launch() const {
    if (!play_.active() || play_.launch_id() != launch_ || !play_.recording_launch())
        throw std::runtime_error("The recording Play process stopped or was replaced: " +
                                 play_.status());
    if (!session_.empty() &&
        (play_.session() != session_ || play_.world_generation() != world_generation_))
        throw std::runtime_error("The recording runtime session or world changed");
}
void GameRecordingTask::stop_play() noexcept {
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
    if (document_.project() != project_ || scene_.asset_id() != scene_id_ || play_.active())
        throw std::runtime_error("Choose the current Scene with Play stopped before recording");
    revision_ = scene_.revision();
    generation_ = document_.generation();
    require_source();
    rate_ = request_.rate();
    if (!request_.input_recording.empty())
        replay_input_ =
            read_input_recording(project_, std::filesystem::u8path(request_.input_recording));
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
    status_ = "Preparing fresh game recording";
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
    if (images_.busy() || frame_image_done_ || folder_.empty() ||
        completed_ >= unsigned(request_.frames))
        return;
    const auto expected = folder_ / request_.plan().filename(completed_);
    if (images_.completed_image() == expected && images_.error().empty()) {
        frame_image_done_ = true;
        ++captured_;
        asset_storage::ordinary(expected);
        const auto size = std::filesystem::file_size(asset_detail::native_io_path(expected));
        if (size > asset_detail::sequence_snapshot_byte_limit - png_bytes_ ||
            audio_frames_ * 4 > asset_detail::sequence_snapshot_byte_limit - png_bytes_ - size)
            throw std::runtime_error("Recording PNG and PCM snapshots exceed 256 MiB");
        png_bytes_ += std::size_t(size);
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
        ++completed_;
        if (!retiring_) {
            if (completed_ == unsigned(request_.frames))
                encode();
            else
                begin_frame();
        }
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
    frame_image_done_ = false;
    stable_ = 0;
    const auto filename = request_.plan().filename(completed_);
    const auto path = folder_ / filename;
    asset_storage::ordinary(path);
    if (std::filesystem::exists(path))
        throw std::runtime_error("A recording frame destination was created by another writer");
    frame_rows_.push_back(
        {{"file", filename},
         {"time_seconds", double(audio_frames_) / asset_detail::movie_audio_sample_rate},
         {"audio_start_frame", audio_frames_},
         {"presentation_seconds", play_.timing().at("presentation_seconds")},
         {"simulation_tick", play_.timing().at("tick")},
         {"alpha", play_.timing().at("alpha")}});
    const auto accepted =
        host_.pixels
            ? images_.selected_path(ViewImageTask::View::Texture, path, project_, {},
                                    image_message_, ViewImageFormat::Png, host_.pixels(completed_))
            : images_.selected_path(ViewImageTask::View::Game, path, project_, scene_id_,
                                    image_message_, ViewImageFormat::Png);
    if (!accepted)
        throw std::runtime_error("Recording PNG request failed: " + images_.status());
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
    const auto destination = folder_ / "audio.s16le";
    const auto expected = audio_frames_ * 4;
    file_ = std::async(std::launch::async, [destination, expected, pcm = std::move(audio.pcm)] {
        asset_storage::ordinary(destination);
        const auto path = asset_detail::native_io_path(destination);
        const auto size = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
        if (size != expected || pcm.size() > asset_detail::avi_game_audio_byte_limit - size)
            throw std::runtime_error("Recording PCM file changed before append");
        std::ofstream out;
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.open(path, std::ios::binary | std::ios::app);
        out.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(pcm.size()));
        out.close();
        return pcm.size();
    });
    file_job_ = FileJob::Pcm;
    phase_ = Phase::Pcm;
    status_ = "Saving completed game audio interval";
}
void GameRecordingTask::encode() {
    stop_play();
    std::vector<std::string> names;
    for (unsigned frame = 0; frame < completed_; ++frame)
        names.push_back(request_.plan().filename(frame));
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
    for (unsigned i = 0; i < captured_; ++i)
        frames.push_back(frame_rows_.at(i));
    Json value{{"format", "forge.game-recording"},
               {"version", 1},
               {"mode", "offline_game"},
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
               {"completed_frames", completed_},
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
    if (!replay_input_.is_null()) {
        const auto text = replay_input_.dump();
        value["input_recording"] = {
            {"admitted", replay_admitted_},
            {"file", "input-recording.json"},
            {"source", replay_input_.at("source")},
            {"ticks", replay_input_.at("frames").size()},
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
    ImGui::TextWrapped("Fresh offline game run with synchronized audio. Game logic and physics run "
                       "at the project's fixed rate.");
    if (phase_ == Phase::Configure && !retiring_) {
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
        ImGui::TextUnformatted("Input recording (optional)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##game-recording-input", &request_.input_recording);
        FORGE_UI_PROBE("game-recording:input");
        ImGui::TextWrapped(
            "Use Recordings/<name>.json from this project to replay saved fixed gameplay actions.");
        bool valid = false;
        try {
            (void)request_.rate();
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
            "Includes PNG frames, stereo 48 kHz PCM16, a movie and a recording receipt. This run "
            "uses fresh user data. Interactive input and scene transitions are unavailable.");
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
