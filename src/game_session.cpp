#include <cmath>
#include <forge/game_session.hpp>
#include <limits>
namespace forge {
namespace {
struct Mutation {
    bool& busy;
    explicit Mutation(bool& flag) : busy(flag) { busy = true; }
    ~Mutation() { busy = false; }
};
} // namespace
GameSession::GameSession(GameSessionConfig config)
    : config_(std::move(config)), clock_(config_.clock) {
    config_.physics.validate();
    if (config_.controls)
        config_.modules.push_back(config_.controls->module());
    if (config_.ui && config_.content_root.empty())
        throw std::runtime_error("game.session: Runtime UI requires a content root");
}
GameSession::~GameSession() = default;
void GameSession::owner() const {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("game.session: Operation requires its owner thread");
    if (changing_)
        throw std::runtime_error("game.session: Reentrant lifecycle operation rejected");
}
void GameSession::require_active() const {
    owner();
    if (!active_ || faulted_)
        throw std::runtime_error("game.session: A healthy active scene is required");
}
std::uint64_t GameSession::prepare(const Json& snapshot,
                                   const std::function<void(Scene&)>& restore_supported_state,
                                   const Json& module_state) {
    owner();
    Mutation guard(changing_);
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("game.session: Transition generation exhausted");
    // Starting a new request retires the previous unpublished request. The active
    // world and its clock/input remain untouched even if the new request fails.
    const auto superseded = pending_;
    discard_candidate();
    const auto ticket = ++generation_;
    loading_ = {ticket, superseded, "preparing", "structure", {}, {}, 0, 0, false};
    try {
        auto next = std::make_unique<RuntimeWorld>(module_, config_.modules, config_.physics,
                                                   config_.audio, config_.content_root, config_.ui);
        next->simulation.audio_paused(true);
        next->simulation.input().configure(config_.input);
        next->scene.restore_snapshot(snapshot);
        next->engine.world().modules().scene_ready();
        if (restore_supported_state)
            restore_supported_state(next->scene);
        next->engine.world().modules().restore_state(module_state);
        const bool physics_ready = next->physics()->prepare_assets();
        if (physics_ready) {
            next->physics()->synchronize(0); // No elapsed physics time.
            next->simulation.restore_input_tick(0);
            next->simulation.reset_presentation();
            next->simulation.sync_audio();
        }
        auto preparation = config_.preparation ? config_.preparation(ticket) : nullptr;
        if (config_.preparation && !preparation)
            throw std::runtime_error("game.session: Host returned no preparation owner");
        candidate_ = std::move(next);
        candidate_initialized_ = physics_ready;
        candidate_preparation_ = std::move(preparation);
        pending_ = ticket;
        progress_ = {physics_ready && !candidate_preparation_,
                     !physics_ready           ? "collision"
                     : candidate_preparation_ ? "resources"
                                              : "structural",
                     0, 0};
        error_.clear();
        loading_.state = progress_.ready ? "ready" : "loading";
        loading_.stage = progress_.stage;
        loading_.can_cancel = true;
        return ticket;
    } catch (const std::exception& e) {
        error_ = e.what();
        loading_.state = "failed";
        loading_.error_code = "game.scene.prepare";
        loading_.error = error_.substr(0, 1024);
        throw;
    }
}
void GameSession::discard_candidate() {
    candidate_preparation_.reset();
    candidate_.reset();
    candidate_initialized_ = false;
    pending_ = 0;
    progress_ = {};
}
GamePreparationProgress GameSession::poll_candidate() {
    try {
        if (!candidate_initialized_) {
            if (candidate_->physics()->prepare_assets()) {
                candidate_->physics()->synchronize(0);
                candidate_->simulation.restore_input_tick(0);
                candidate_->simulation.reset_presentation();
                candidate_->simulation.sync_audio();
                candidate_initialized_ = true;
                progress_ = {!candidate_preparation_,
                             candidate_preparation_ ? "resources" : "structural", 0, 0};
            } else
                progress_ = {false, "collision", 0, 1};
        }
        if (candidate_initialized_ && candidate_preparation_) {
            auto next = candidate_preparation_->poll(*candidate_);
            if (next.completed > next.total || next.stage.empty() || next.stage.size() > 256)
                throw std::runtime_error("game.session: Invalid preparation progress");
            progress_ = std::move(next);
        }
        loading_.state = progress_.ready ? "ready" : "loading";
        loading_.stage = progress_.stage;
        loading_.completed = progress_.completed;
        loading_.total = progress_.total;
        return progress_;
    } catch (const std::exception& e) {
        error_ = e.what();
        discard_candidate();
        loading_.state = "failed";
        loading_.error_code = "game.scene.resources";
        loading_.error = error_.substr(0, 1024);
        loading_.can_cancel = false;
        throw;
    } catch (...) {
        error_ = "Non-standard native exception in scene preparation";
        discard_candidate();
        loading_.state = "failed";
        loading_.error_code = "game.scene.native_exception";
        loading_.error = error_;
        loading_.can_cancel = false;
        throw;
    }
}
GamePreparationProgress GameSession::poll_preparation(std::uint64_t ticket) {
    owner();
    if (!candidate_ || ticket != pending_)
        throw std::runtime_error("game.session: Stale or missing prepared scene");
    Mutation guard(changing_);
    return poll_candidate();
}
void GameSession::activate(std::uint64_t ticket, RuntimeClock::Time now, bool run) {
    owner();
    if (!candidate_ || ticket != pending_)
        throw std::runtime_error("game.session: Stale or missing prepared scene");
    Mutation guard(changing_);
    if (!poll_candidate().ready)
        throw std::runtime_error("game.session: Required scene resources are not ready");
    // Finish potentially failing preparation before pausing or replacing active.
    candidate_->simulation.restore_input_tick(0);
    candidate_->simulation.reset_presentation();
    const auto controls = config_.controls ? candidate_->engine.world().services().game() : nullptr;
    if (active_)
        active_->simulation.audio_paused(true);
    clock_.restore_tick(0, now); // A new world starts a new simulation timeline.
    active_.swap(candidate_);
    active_preparation_.swap(candidate_preparation_);
    if (active_preparation_)
        active_preparation_->activate();
    pending_ = 0;
    faulted_ = false;
    error_.clear();
    discard_candidate(); // Stops old consumers/world before new gameplay starts.
    loading_.state = "activated";
    loading_.can_cancel = false;
    // Resume is post-commit. A device/native callback failure is a session fault,
    // never a claim that the old world has been restored.
    try {
        if (config_.controls)
            config_.controls->activate(controls);
        active_->simulation.audio_paused(!run);
        if (run)
            clock_.resume(now);
    } catch (const std::exception& e) {
        faulted_ = true;
        error_ = e.what();
        loading_.state = "faulted";
        loading_.error_code = "game.scene.activate";
        loading_.error = error_.substr(0, 1024);
        throw;
    }
}
void GameSession::cancel(std::uint64_t ticket) {
    owner();
    if (!candidate_ || ticket != pending_)
        throw std::runtime_error("game.session: Stale or missing prepared scene");
    Mutation guard(changing_);
    discard_candidate();
    loading_.state = "cancelled";
    loading_.can_cancel = false;
}
void GameSession::unload(RuntimeClock::Time now) {
    owner();
    Mutation guard(changing_);
    clock_.pause(now);
    discard_candidate();
    active_preparation_.reset();
    active_.reset();
    pending_ = 0;
    faulted_ = false;
    error_.clear();
    loading_ = {};
}
LoadingState GameSession::loading_state() const {
    owner();
    return loading_;
}
void GameSession::pause(RuntimeClock::Time now) {
    require_active();
    Mutation guard(changing_);
    active_->simulation.audio_paused(true);
    active_->simulation.input().release_all();
    clock_.pause(now);
}
void GameSession::resume(RuntimeClock::Time now) {
    require_active();
    Mutation guard(changing_);
    if (!clock_.paused())
        return;
    active_->simulation.input().release_all();
    active_->simulation.reset_presentation();
    active_->simulation.audio_paused(false);
    clock_.resume(now);
}
void GameSession::tick(float dt) {
    try {
        active_->simulation.tick(dt);
    } catch (const std::exception& e) {
        faulted_ = true;
        error_ = e.what();
        throw;
    } catch (...) {
        faulted_ = true;
        error_ = "Non-standard native exception in fixed tick";
        throw;
    }
}
void GameSession::step() {
    require_active();
    Mutation guard(changing_);
    clock_.step([&](float dt) { tick(dt); });
}
void GameSession::advance(RuntimeClock::Time now) {
    require_active();
    Mutation guard(changing_);
    clock_.advance(now, [&](float dt) { tick(dt); });
}
void GameSession::control_frame() {
    require_active();
    Mutation guard(changing_);
    try {
        if (control_frame_ == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Control-frame sequence exhausted");
        const auto& input = active_->simulation.input().latch_controls(++control_frame_);
        active_->engine.world().modules().control_frame(input);
    } catch (const std::exception& e) {
        faulted_ = true;
        error_ = std::string("Game control callback failed: ") + e.what();
        throw;
    } catch (...) {
        faulted_ = true;
        error_ = "Game control callback failed";
        throw;
    }
}
void GameSession::input(const std::vector<InputEvent>& events) {
    require_active();
    active_->simulation.input().submit(events);
}
void GameSession::input_map(InputMap input) {
    owner();
    Mutation guard(changing_);
    if (faulted_)
        throw std::runtime_error("Cannot change input in a faulted session");
    if (active_)
        active_->simulation.input().replace_map(input);
    config_.input = std::move(input);
    if (candidate_)
        candidate_->simulation.input().configure(config_.input);
}
void GameSession::master_volume(float value) {
    owner();
    Mutation guard(changing_);
    if (!std::isfinite(value) || value < 0 || value > 1 || faulted_)
        throw std::runtime_error("Invalid master volume or faulted session");
    for (auto* world : {active_.get(), candidate_.get()})
        if (world && world->engine.services().available(Capability::Audio))
            std::static_pointer_cast<AudioRuntime>(world->engine.services().audio())
                ->master_volume(value);
    if (config_.audio)
        config_.audio->master_volume = value;
}
RuntimeWorld& GameSession::active() {
    require_active();
    return *active_;
}
Json GameSession::presentation() const {
    require_active();
    return active_->simulation.presentation(clock_.alpha());
}
Json GameSession::status() const {
    owner();
    return {{"state", faulted_          ? "faulted"
                      : !active_        ? "empty"
                      : clock_.paused() ? "paused"
                                        : "running"},
            {"clock", clock_.status()},
            {"prepared_ticket", pending_},
            {"preparation",
             {{"scope", config_.preparation ? "host" : "structural"},
              {"ready", progress_.ready},
              {"stage", progress_.stage},
              {"completed", progress_.completed},
              {"total", progress_.total}}},
            {"error", error_}};
}
} // namespace forge
