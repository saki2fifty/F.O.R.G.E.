#include "live_audio_capture.hpp"
#include "audio_capture_access.hpp"
#include "base64.hpp"
#include "input_recording_protocol.hpp"
#include "recorded_pcm.hpp"
#include <stdexcept>

namespace forge {
struct LiveAudioCapture::Preparation final : GameScenePreparation {
    LiveAudioCapture& owner;
    std::unique_ptr<GameScenePreparation> delegate;
    std::uint64_t ticket;
    std::shared_ptr<AudioRuntime> next;
    Json witness;
    Json::number_unsigned_t* boundary = nullptr;
    Failure failure = Failure::None;
    bool attempted = false;
    Preparation(LiveAudioCapture& capture, std::uint64_t value) : owner(capture), ticket(value) {}
    GamePreparationProgress poll(RuntimeWorld& world) override {
        const auto result = delegate->poll(world);
        if (!result.ready || attempted || !owner.following_)
            return result;
        attempted = true;
        if (owner.visits_.size() >= input_recording_scene_limit) {
            failure = Failure::VisitLimit;
            return result;
        }
        try {
            next = std::static_pointer_cast<AudioRuntime>(world.engine.services().audio());
            if (!next)
                throw std::runtime_error("The candidate World has no audio service");
            AudioCaptureAccess::prepare(*next);
            witness = owner.visit(world.scene, ticket, 0);
            boundary = &witness.at("audio_frame").get_ref<Json::number_unsigned_t&>();
        } catch (...) {
            // Recording is an observer of ordinary Play. It must not reject a
            // game Scene because preparing that observer failed.
            failure = Failure::Preparation;
        }
        return result;
    }
    void activate() noexcept override {
        delegate->activate();
        owner.adopt(*this);
    }
};
LiveAudioCapture::LiveAudioCapture(std::filesystem::path project) : project_(std::move(project)) {}
LiveAudioCapture::~LiveAudioCapture() {
    if (audio_)
        try {
            AudioCaptureAccess::end(*audio_);
        } catch (...) {
            // The World/device shutdown owner still joins the producer.
        }
}
Json LiveAudioCapture::visit(const Scene& scene, std::uint64_t generation,
                             std::uint64_t audio_frame) const {
    return {{"generation", generation},
            {"scene", scene.asset_id()},
            {"source", input_recording_source(scene.snapshot(), project_)},
            {"audio_frame", audio_frame}};
}
std::unique_ptr<GameScenePreparation>
LiveAudioCapture::prepare(std::unique_ptr<GameScenePreparation> delegate, std::uint64_t ticket) {
    if (!delegate || !ticket)
        throw std::runtime_error("Live recording requires a valid Scene preparation adapter");
    if (!following_)
        return delegate;
    try {
        auto prepared = std::make_unique<Preparation>(*this, ticket);
        prepared->delegate = std::move(delegate);
        return prepared;
    } catch (...) {
        // Keep the gameplay delegate intact if allocating its optional observer
        // fails; only the recording lease reports preparation failure.
        failure_ = Failure::Preparation;
        following_ = false;
        return delegate;
    }
}
void LiveAudioCapture::adopt(Preparation& prepared) noexcept {
    if (!following_)
        return;
    if (prepared.failure != Failure::None || !prepared.next || !prepared.boundary) {
        failure_ = prepared.failure == Failure::None ? Failure::Preparation : prepared.failure;
        following_ = false;
        return;
    }
    auto& visits = visits_.get_ref<Json::array_t&>();
    if (visits.size() >= input_recording_scene_limit || visits.size() == visits.capacity()) {
        failure_ = Failure::VisitLimit;
        following_ = false;
        return;
    }
    const auto boundary = AudioCaptureAccess::handoff(*audio_, *prepared.next);
    if (!boundary) {
        failure_ = Failure::Handoff;
        following_ = false;
        return;
    }
    // Storage, JSON keys and vector capacity were prepared before publication.
    *prepared.boundary = *boundary;
    visits.push_back(std::move(prepared.witness));
    audio_ = std::move(prepared.next);
}
Json LiveAudioCapture::status(std::uint64_t active_generation) const {
    auto result = AudioCaptureAccess::status(*audio_);
    result["world_generation"] = initial_world_;
    result["scene"] = initial_scene_;
    result["active_generation"] = active_generation;
    result["capture_scope"] = session_scope_ ? "session" : "scene";
    if (session_scope_) {
        result["profile"] = "live-session-game-v1";
        result["visits"] = visits_;
    }
    if (failure_ != Failure::None && result.at("error").get_ref<const std::string&>().empty()) {
        const char* error = "Live recording Scene handoff failed; completed prefix retained";
        if (failure_ == Failure::Preparation)
            error = "Live recording Scene capture preparation failed; completed prefix retained";
        else if (failure_ == Failure::VisitLimit)
            error = "Live recording exceeds 64 Scene visits; completed prefix retained";
        else if (failure_ == Failure::World)
            error = "Live recording World changed without a capture handoff; prefix retained";
        result["state"] = "failed";
        result["error"] = error;
    }
    return result;
}
Json LiveAudioCapture::command(const Json& request, std::uint64_t active_generation,
                               const std::shared_ptr<AudioRuntime>& active_audio,
                               const Scene* active_scene, bool settled) {
    const auto operation = request.value("operation", std::string{});
    if ((operation != "begin" && operation != "read" && operation != "end") ||
        !request.contains("generation") || !request.at("generation").is_number_unsigned())
        throw std::runtime_error("Live audio capture requires a world generation and "
                                 "begin/read/end operation");
    if (operation == "begin") {
        if (!active_generation || !active_audio || !active_scene || !settled ||
            request.at("generation") != active_generation || request.contains("capture_generation"))
            throw std::runtime_error("Begin live audio capture in the current settled world "
                                     "without an earlier capture generation");
        const auto scope = request.value("capture_scope", std::string("scene"));
        if (scope != "scene" && scope != "session")
            throw std::runtime_error("Live recording capture scope must be scene or session");
        if (audio_) {
            const auto prior = AudioCaptureAccess::status(*audio_);
            if (prior.at("state") == "recording" || prior.at("queued_frames") != 0)
                throw std::runtime_error("End and drain the earlier live audio capture "
                                         "before starting another");
        }
        auto visits = Json::array();
        if (scope == "session") {
            visits.get_ref<Json::array_t&>().reserve(input_recording_scene_limit);
            visits.push_back(visit(*active_scene, active_generation, 0));
        }
        AudioCaptureAccess::begin(*active_audio);
        audio_ = active_audio;
        initial_world_ = active_generation;
        initial_scene_ = active_scene->asset_id();
        session_scope_ = following_ = scope == "session";
        failure_ = Failure::None;
        visits_.swap(visits);
    } else {
        if (request.contains("capture_scope") || !audio_ ||
            request.at("generation") != initial_world_ || !request.contains("capture_generation") ||
            !request.at("capture_generation").is_number_unsigned() ||
            request.at("capture_generation") !=
                AudioCaptureAccess::status(*audio_).at("generation"))
            throw std::runtime_error("Live audio capture has a stale world or capture generation");
        if (operation == "read" &&
            (!request.contains("maximum_frames") ||
             !request.at("maximum_frames").is_number_unsigned() ||
             request.at("maximum_frames").get<std::uint64_t>() < 1 ||
             request.at("maximum_frames").get<std::uint64_t>() > 4096 ||
             !request.contains("audio_frame") || !request.at("audio_frame").is_number_unsigned() ||
             request.at("audio_frame") != AudioCaptureAccess::status(*audio_).at("read_frames")))
            throw std::runtime_error("Live audio reads require 1..4096 frames and the current "
                                     "sequential audio cursor");
        if (session_scope_ && following_ && visits_.back().at("generation") != active_generation) {
            failure_ = Failure::World;
            following_ = false;
        }
        if (operation == "end") {
            following_ = false;
            AudioCaptureAccess::end(*audio_);
        }
    }
    Json response;
    if (operation == "read") {
        const auto cursor = AudioCaptureAccess::status(*audio_).at("read_frames");
        const auto samples =
            AudioCaptureAccess::read(*audio_, request.at("maximum_frames").get<unsigned>());
        std::vector<std::byte> pcm;
        pcm.reserve(samples.size() * 2);
        append_recorded_pcm(pcm, samples);
        response["audio_capture_chunk"] = {{"format", "s16le-stereo-48000-v1"},
                                           {"start_audio_frame", cursor},
                                           {"audio_frames", samples.size() / 2},
                                           {"data", encode_base64(pcm)}};
    }
    response["audio_capture"] = status(active_generation);
    return response;
}
} // namespace forge
