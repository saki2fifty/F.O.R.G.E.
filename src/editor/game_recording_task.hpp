#pragma once
#include "../native_io_path.hpp"
#include "document.hpp"
#include "game_recording_request.hpp"
#include "play.hpp"
#include "view_image.hpp"
#include <array>
#include <fstream>

namespace forge::ui {
struct GameRecordingHost {
    std::function<void(const std::filesystem::path&)> start;
    std::function<void()> stop;
    std::function<std::string()> unavailable;
    // Trusted detached frame adapter for headless hosts. The native editor uses
    // the existing Game texture/readback route when this callback is absent.
    std::function<ViewImageTask::PixelInput(unsigned)> pixels;
    std::function<void(ViewImageTask&, Diligent::IDeviceContext*, const std::filesystem::path&,
                       AssetId, std::string&)>
        poll_images;
    std::function<void(ViewImageTask&, DiligentPresentation&, Diligent::IDeviceContext*,
                       Diligent::ITextureView*, std::string&)>
        capture_image;
    std::function<void()> abandon_readback;
    SDL_Window* window = nullptr;
};
class GameRecordingTask final : public ViewImageWork {
    enum class Phase {
        Configure,
        Prepare,
        Launch,
        Begin,
        Image,
        Advance,
        Pcm,
        Encode,
        Retire,
        Done
    };
    enum class FileJob { None, Prepare, Pcm, Receipt, Normalize };
    struct LiveState {
        bool render_ready = false, begin_submitted = false, end_requested = false,
             end_submitted = false, normalized = false, image_requested = false,
             audio_discard = false;
        std::uint64_t capture = 0, raw_audio_frames = 0;
        std::optional<std::uint64_t> origin;
        std::size_t last_png_bytes = 0;
        std::size_t movie_png_bytes = 0;
        unsigned requested_frame = 0;
        Json images = Json::array();
        Json last_image;
    };
    std::optional<LiveState> live_;
    static constexpr std::size_t live_audio_byte_limit =
        asset_detail::avi_game_audio_byte_limit + 16 * asset_detail::movie_audio_sample_rate;
    Scene& scene_;
    SceneDocument& document_;
    PlaySession& play_;
    GameRecordingRequest request_;
    Json replay_input_;
    bool replay_submitted_ = false;
    bool replay_admitted_ = false;
    GameRecordingHost host_;
    std::filesystem::path worker_, project_, folder_, previous_user_data_;
    AssetId scene_id_, frame_scene_id_, sample_id_ = AssetId::generate();
    std::uint64_t revision_ = 0, generation_ = 0, audio_frames_ = 0;
    asset_detail::MovieFrameRate rate_;
    unsigned completed_ = 0, captured_ = 0, stable_ = 0;
    std::size_t png_bytes_ = 0;
    Phase phase_ = Phase::Configure;
    FileJob file_job_ = FileJob::None;
    bool finished_ = false, owns_folder_ = false, retiring_ = false, show_ = true;
    bool frame_image_done_ = false;
    std::string launch_, session_, source_digest_, status_ = "Choose recording output";
    std::string error_, reason_, terminal_, image_message_, validation_, started_at_;
    std::uint64_t world_generation_ = 0;
    std::uint64_t frame_generation_ = 0;
    std::uint64_t observed_audio_frames_ = 0;
    Json visits_ = Json::array();
    ViewImageTask images_;
    bool readback_used_ = false, user_data_scoped_ = false;
    std::future<std::size_t> file_;
    std::future<asset_detail::AviSequenceInfo> encoder_;
    std::optional<asset_detail::AviSequenceInfo> movie_;
    std::stop_source encoder_stop_;
    std::chrono::steady_clock::time_point deadline_;
    Json frame_rows_ = Json::array();
    FileDialog dialog_;
    std::array<char, 2048> parent_{};
    std::array<char, 128> name_{};

  public:
    static std::string unavailable(const PlaySession&, const std::string& mode);
    GameRecordingTask(Scene& scene, SceneDocument& document, PlaySession& play,
                      std::filesystem::path worker, GameRecordingRequest request,
                      GameRecordingHost host, bool configure = false)
        : scene_(scene), document_(document), play_(play), request_(std::move(request)),
          host_(std::move(host)), worker_(std::move(worker)), project_(document.project()),
          previous_user_data_(std::filesystem::u8path(play.user_data_override())),
          scene_id_(scene.asset_id()), revision_(scene.revision()),
          generation_(document.generation()) {
        document_.check_ownership();
        if (!host_.start || !host_.stop || worker_.empty() ||
            (!configure && request_.recording_mode == "offline" && play_.active()) ||
            (!host_.pixels && (!host_.poll_images || !host_.capture_image)))
            throw std::runtime_error(
                "Game recording requires an idle Play host and the packaged worker");
        const auto proposed = request_.destination.empty()
                                  ? project_.parent_path() / "GameRecording"
                                  : request_.destination;
        const auto parent = path_utf8(proposed.parent_path()),
                   name = path_utf8(proposed.filename());
        if (parent.size() >= parent_.size() || name.size() >= name_.size())
            throw std::runtime_error("Recording folder path exceeds its input limit");
        std::copy(parent.begin(), parent.end(), parent_.begin());
        std::copy(name.begin(), name.end(), name_.begin());
        if (!configure)
            start();
    }
    ~GameRecordingTask() override;
    bool busy() const override { return !finished_; }
    std::string_view kind() const override { return "game_recording"; }
    std::optional<SequenceFrameExtent> extent() const override {
        return !finished_ && phase_ != Phase::Configure && phase_ != Phase::Encode && !retiring_
                   ? request_.output.extent()
                   : std::nullopt;
    }
    std::optional<TemporalFrameSample> temporal_sample() const override {
        if (live_ || phase_ != Phase::Image || retiring_)
            return {};
        return TemporalFrameSample{
            sample_id_, request_.capture_scope == "session"
                            ? double(audio_frames_) / asset_detail::movie_audio_sample_rate
                            : play_.timing().at("presentation_seconds").get<double>()};
    }
    std::filesystem::path preview_path(std::optional<unsigned> frame) const override {
        if (!finished_ || !frame || *frame >= captured_)
            throw std::runtime_error("Choose a completed PNG frame from this game recording");
        return folder_ / request_.plan().filename(*frame);
    }
    Json state() const override {
        Json value{{"kind", kind()},
                   {"state", phase_name()},
                   {"status", status_},
                   {"error", error_},
                   {"reason", reason_},
                   {"folder", path_utf8(folder_)},
                   {"scene", scene_id_},
                   {"scene_revision", revision_},
                   {"document_generation", generation_},
                   {"planned_frames", request_.frames},
                   {"completed_frames", completed_media_frames()},
                   {"captured_png_frames", captured_},
                   {"audio_frames", audio_frames_},
                   {"width", request_.output.width},
                   {"height", request_.output.height},
                   {"fps", request_.fps},
                   {"capture_scope", request_.capture_scope},
                   {"recording_mode", request_.recording_mode},
                   {"jpeg_quality", request_.output.jpeg_quality},
                   {"cancel_requested", retiring_ && terminal_ == "cancelled"}};
        if (movie_)
            value["movie"] = movie_document();
        return value;
    }
    bool cancel() override {
        if (finished_ || retiring_ || terminal_ == "complete")
            return false;
        abort("Game recording cancelled", false);
        return true;
    }
    void game_frame(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    Diligent::ITextureView* texture, bool ready) override {
        if (live_ && phase_ == Phase::Launch) {
            live_->render_ready =
                texture && ready &&
                texture->GetTexture()->GetDesc().Width == unsigned(request_.output.width) &&
                texture->GetTexture()->GetDesc().Height == unsigned(request_.output.height);
            return;
        }
        if (phase_ != Phase::Image || retiring_ || host_.pixels || !texture)
            return;
        if (live_ && (!live_->image_requested || frame_image_done_))
            return;
        const auto& desc = texture->GetTexture()->GetDesc();
        if (!ready || desc.Width != unsigned(request_.output.width) ||
            desc.Height != unsigned(request_.output.height)) {
            stable_ = 0;
            return;
        }
        const auto required = live_ ? 1u : 3u;
        if (stable_ < required)
            ++stable_;
        if (stable_ == required) {
            if (live_) {
                try {
                    if (live_->end_requested && !live_->last_image.is_null()) {
                        (void)images_.cancel(image_message_);
                        live_->images.erase(live_->images.size() - 1);
                        live_->image_requested = false;
                        frame_image_done_ = true;
                        return;
                    }
                    sample_live_frame();
                    live_->image_requested = false;
                } catch (const std::exception& error) {
                    abort(error.what(), true);
                    return;
                }
            }
            readback_used_ = true;
            host_.capture_image(images_, presentation, context, texture, image_message_);
        }
    }
    void poll(Diligent::IDeviceContext* context) override {
        if (finished_)
            return;
        try {
            if (host_.poll_images)
                host_.poll_images(images_, context, project_, frame_scene_id_, image_message_);
            else
                (void)images_.poll_files(image_message_);
            observe_image();
            if (file_.valid() &&
                file_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                complete_file();
            if (finished_)
                return;
            if (encoder_.valid() &&
                encoder_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    movie_ = encoder_.get();
                    if (!retiring_) {
                        terminal_ = "complete";
                        retiring_ = true;
                    }
                } catch (const std::exception& e) {
                    if (!retiring_)
                        throw;
                    if (terminal_ == "failed" && error_.empty())
                        error_ = e.what();
                }
            }
            if (retiring_) {
                retire();
                return;
            }
            if (phase_ == Phase::Configure || phase_ == Phase::Encode)
                return;
            require_source();
            if (live_) {
                poll_live();
                return;
            }
            if (phase_ == Phase::Launch) {
                require_launch();
                if (play_.control_ready()) {
                    session_ = play_.session();
                    world_generation_ = play_.world_generation();
                    if (!replay_input_.is_null() && request_.capture_scope == "scene") {
                        if (!replay_submitted_) {
                            if (!play_.submit_input_recording("replay", replay_input_))
                                throw std::runtime_error(
                                    "Offline input replay could not be queued");
                            replay_submitted_ = true;
                            return;
                        }
                        if (play_.input_recording_pending())
                            return;
                        if (!play_.input_recording_error().empty() ||
                            play_.input_recording_status().value("mode", "") != "replaying")
                            throw std::runtime_error("Offline input replay was rejected: " +
                                                     play_.input_recording_error());
                        replay_admitted_ = true;
                    }
                    if (!play_.paused() || play_.timing().at("tick") != 0 ||
                        !play_.begin_recording(
                            request_.fps, unsigned(request_.frames), request_.capture_scope,
                            request_.capture_scope == "session" ? replay_input_ : Json()))
                        throw std::runtime_error("Recording could not begin from fresh tick zero");
                    phase_ = Phase::Begin;
                    status_ = "Starting offline game recording";
                }
            } else if (phase_ == Phase::Begin) {
                require_launch();
                if (!play_.recording_pending() &&
                    play_.recording_status().value("started", false)) {
                    if (request_.capture_scope == "session" && !replay_input_.is_null())
                        replay_admitted_ = true;
                    begin_frame();
                }
            } else if (phase_ == Phase::Image) {
                require_launch();
                if (frame_image_done_ && !images_.busy() && play_.control_ready()) {
                    if (!play_.advance_recording(completed_))
                        throw std::runtime_error("Recording interval could not be queued");
                    phase_ = Phase::Advance;
                    status_ = "Mixing game audio and advancing fixed simulation";
                }
            } else if (phase_ == Phase::Advance) {
                require_launch();
                if (!play_.recording_pending()) {
                    if (auto audio = play_.take_recording_frame())
                        write_pcm(std::move(*audio));
                    else if (request_.capture_scope == "session" && play_.control_ready()) {
                        if (!play_.advance_recording(completed_))
                            throw std::runtime_error("Pending recording interval could not resume");
                        status_ = "Preparing Scenes and completing the current audio interval";
                    }
                }
            }
            if (phase_ != Phase::Prepare && phase_ != Phase::Pcm &&
                std::chrono::steady_clock::now() >= deadline_)
                throw std::runtime_error(
                    "Game recording needs a rendered Game view with an active camera and ready "
                    "resources; no frame was available within 60 seconds");
        } catch (const std::exception& e) {
            if (file_job_ == FileJob::Receipt) {
                error_ = e.what();
                terminal_ = "failed";
                finished_ = true;
                phase_ = Phase::Done;
                status_ = "Game recording receipt failed: " + error_;
            } else
                abort(e.what(), true);
        }
    }
    void draw() override;

  private:
    void start();
    void require_source() const;
    void require_launch();
    void stop_play() noexcept;
    void abort(std::string, bool);
    void observe_image();
    void complete_file();
    void begin_frame();
    void write_pcm(RecordedAudioFrame);
    void append_pcm(std::vector<std::byte>, std::uint64_t, std::size_t);
    void poll_live();
    bool service_live_audio();
    void sample_live_frame();
    bool live_world_ready() const;
    void remember_recording_progress(const Json&);
    void fill_live_frames(unsigned);
    void complete_live_image(std::size_t);
    void normalize_live_audio();
    unsigned completed_media_frames() const {
        if (!live_)
            return completed_;
        const auto samples = live_->normalized ? audio_frames_
                             : live_->origin && audio_frames_ >= *live_->origin
                                 ? audio_frames_ - *live_->origin
                                 : 0;
        return std::min(completed_, asset_detail::avi_game_frames_at_audio(
                                        rate_, unsigned(request_.frames), samples));
    }
    void encode();
    void retire();
    void write_receipt();
    Json receipt() const;
    Json movie_document() const;
    const char* phase_name() const;
};
} // namespace forge::ui
