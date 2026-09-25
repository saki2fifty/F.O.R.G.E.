#include "play_scene_admission.hpp"
#include <atomic>
#include <forge/runtime_ui.hpp>
#include <forge/services.hpp>
#include <stdexcept>
namespace forge {
namespace {
std::optional<Json> runtime_ui_snapshot(RuntimeWorld& world, const std::string& session,
                                        std::uint64_t ticket) {
    auto services = world.engine.services();
    if (!services.available(Capability::Ui))
        return std::nullopt;
    auto ui = std::static_pointer_cast<UiRuntime>(services.ui());
    if (!ui)
        return std::nullopt;
    auto snapshot = ui->snapshot(world.scene, session, ticket, 0, true);
    if (snapshot.contains("errors") && !snapshot.at("errors").empty())
        throw std::runtime_error("Scene preparation: " + snapshot.at("errors").dump());
    return snapshot;
}
enum class AckResult { Pending, Accepted, Rejected };
struct PendingRecord {
    std::uint64_t ticket = 0;
    Json envelope; // Frozen once runtime readiness publishes the snapshot.
    bool exported = false;
    AckResult ack = AckResult::Pending;
    bool ack_observed = false;
    std::atomic<bool> closed{false};
    bool first_accepted = false;
    std::string first_diagnostic;
};
struct Adapter final : GameScenePreparation {
    std::weak_ptr<PendingRecord> pending;
    std::thread::id owner = std::this_thread::get_id();
    std::string session;
    PlaySceneAdmission::PublicationValidator validator;
    bool runtime_ready = false;
    Adapter(std::weak_ptr<PendingRecord> p, std::string s,
            PlaySceneAdmission::PublicationValidator v)
        : pending(std::move(p)), session(std::move(s)), validator(std::move(v)) {}
    GamePreparationProgress poll(RuntimeWorld& world) override {
        if (owner != std::this_thread::get_id())
            throw std::runtime_error("Play scene admission requires the owner thread");
        auto record = pending.lock();
        if (!record || record->closed.load())
            throw std::runtime_error("Play scene admission: channel was closed");
        if (!runtime_ready) {
            if (!world.prepare_scene_resources())
                return {false, "animation", 0, 4};
            // Allow the host to reject publication of the envelope (e.g.
            // because the resulting post-activation response would not fit
            // the protocol bound). The thrown diagnostic propagates back to
            // the GameSession loading state; the active world is untouched.
            if (validator)
                validator(world, record->ticket);
            runtime_ready = true;
            Json envelope = Json::object();
            envelope["session"] = session;
            envelope["ticket"] = record->ticket;
            envelope["scene"] = world.simulation.presentation(1);
            const auto ui = runtime_ui_snapshot(world, session, record->ticket);
            envelope["ui"] = ui ? *ui : Json(nullptr);
            const auto bytes = envelope.dump().size();
            if (bytes > kPlaySceneAdmissionEnvelopeBytes)
                throw std::runtime_error("Play scene admission envelope exceeded 8 MiB");
            record->envelope = std::move(envelope);
            record->exported = true;
        }
        switch (record->ack) {
        case AckResult::Accepted:
            return {true, "ready", 4, 4};
        case AckResult::Rejected:
            throw std::runtime_error("Play scene admission: editor rejected the candidate" +
                                     (record->first_diagnostic.empty()
                                          ? std::string{}
                                          : ": " + record->first_diagnostic));
        case AckResult::Pending:
            return {false, "editor", 3, 4};
        }
        return {false, "editor", 3, 4};
    }
    void activate() noexcept override {
        if (auto record = pending.lock())
            record->closed.store(true);
    }
    ~Adapter() override {
        if (auto record = pending.lock())
            record->closed.store(true);
    }
};
} // namespace
struct PlaySceneAdmission::State {
    std::string session;
    std::thread::id owner = std::this_thread::get_id();
    std::uint64_t last_ticket = 0;
    std::shared_ptr<PendingRecord> pending; // At most one active record.
    void require_owner() const {
        if (owner != std::this_thread::get_id())
            throw std::runtime_error("Play scene admission requires the owner thread");
    }
    bool pending_active() const {
        if (!pending)
            return false;
        if (pending->closed.load())
            return false;
        return true;
    }
};
PlaySceneAdmission::PlaySceneAdmission(std::string session) {
    if (session.empty() || session.size() > kPlaySceneAdmissionSessionBytes)
        throw std::runtime_error("Play scene admission: session must be 1..128 chars");
    state_ = std::make_shared<State>();
    state_->session = std::move(session);
}
PlaySceneAdmission::~PlaySceneAdmission() noexcept {
    if (!state_)
        return;
    if (state_->pending)
        state_->pending->closed.store(true);
    state_.reset();
}
std::unique_ptr<GameScenePreparation> PlaySceneAdmission::prepare(std::uint64_t ticket,
                                                                  PublicationValidator validator) {
    if (!state_)
        throw std::runtime_error("Play scene admission channel is closed");
    state_->require_owner();
    if (ticket == 0)
        throw std::runtime_error("Play scene admission: ticket must be nonzero");
    if (ticket <= state_->last_ticket)
        throw std::runtime_error("Play scene admission: ticket must be strictly increasing");
    if (state_->pending_active())
        throw std::runtime_error("Play scene admission: previous candidate is still pending");
    // Strong exception safety: build the adapter before mutating state. If the
    // adapter allocation throws, state is untouched.
    auto record = std::make_shared<PendingRecord>();
    record->ticket = ticket;
    auto adapter = std::make_unique<Adapter>(std::weak_ptr<PendingRecord>(record), state_->session,
                                             std::move(validator));
    state_->pending = record;
    state_->last_ticket = ticket;
    return adapter;
}
std::optional<Json> PlaySceneAdmission::snapshot() {
    if (!state_)
        return std::nullopt;
    state_->require_owner();
    if (!state_->pending_active() || !state_->pending->exported)
        return std::nullopt;
    return Json(state_->pending->envelope); // Detached copy.
}
std::optional<Json> PlaySceneAdmission::fetch_envelope(const std::string& session,
                                                       std::uint64_t ticket) {
    if (!state_)
        return std::nullopt;
    state_->require_owner();
    if (!state_->pending_active() || !state_->pending->exported)
        return std::nullopt;
    auto& record = *state_->pending;
    if (session != state_->session || ticket != record.ticket)
        return std::nullopt;
    return Json(record.envelope); // Detached copy.
}
std::optional<Json> PlaySceneAdmission::reference() const {
    if (!state_)
        return std::nullopt;
    if (state_->owner != std::this_thread::get_id())
        return std::nullopt;
    if (!state_->pending_active() || !state_->pending->exported)
        return std::nullopt;
    return Json{{"session", state_->session}, {"ticket", state_->pending->ticket}, {"ready", true}};
}
bool PlaySceneAdmission::acknowledge(const std::string& session, std::uint64_t ticket,
                                     bool accepted, const std::string& diagnostic) {
    if (!state_)
        return false;
    if (state_->owner != std::this_thread::get_id())
        return false;
    if (ticket == 0)
        return false;
    if (diagnostic.size() > kPlaySceneAdmissionDiagnosticBytes)
        return false;
    if (!state_->pending_active())
        return false;
    auto& record = *state_->pending;
    if (session != state_->session || ticket != record.ticket)
        return false;
    if (!record.exported)
        return false;
    if (record.ack_observed) {
        // Idempotent identical ack: same verdict and diagnostic accepted.
        if (record.first_accepted != accepted || record.first_diagnostic != diagnostic)
            return false;
        return true;
    }
    record.first_diagnostic = diagnostic;
    record.first_accepted = accepted;
    record.ack = accepted ? AckResult::Accepted : AckResult::Rejected;
    record.ack_observed = true;
    return true;
}
bool PlaySceneAdmission::has_pending() const noexcept {
    if (!state_)
        return false;
    if (state_->owner != std::this_thread::get_id())
        return false;
    return state_->pending_active();
}
} // namespace forge