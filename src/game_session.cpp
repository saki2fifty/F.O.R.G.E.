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
                                   const std::function<void(Scene&)>& restore_supported_state) {
    owner();
    Mutation guard(changing_);
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("game.session: Transition generation exhausted");
    // Starting a new request retires the previous unpublished request. The active
    // world and its clock/input remain untouched even if the new request fails.
    discard_candidate();
    const auto ticket = ++generation_;
    try {
        auto next = std::make_unique<RuntimeWorld>(module_, config_.modules, config_.physics,
                                                   config_.audio, config_.content_root, config_.ui);
        next->simulation.audio_paused(true);
        next->simulation.input().configure(config_.input);
        next->scene.restore_snapshot(snapshot);
        if (restore_supported_state)
            restore_supported_state(next->scene);
        next->physics()->synchronize(0); // Initial realization: no elapsed physics time.
        next->simulation.restore_input_tick(0);
        next->simulation.reset_presentation();
        next->simulation.sync_audio();
        auto preparation = config_.preparation ? config_.preparation(ticket) : nullptr;
        if (config_.preparation && !preparation)
            throw std::runtime_error("game.session: Host returned no preparation owner");
        candidate_ = std::move(next);
        candidate_preparation_ = std::move(preparation);
        pending_ = ticket;
        progress_ = {!candidate_preparation_, candidate_preparation_ ? "resources" : "structural",
                     0, 0};
        error_.clear();
        return ticket;
    } catch (const std::exception& e) {
        error_ = e.what();
        throw;
    }
}
void GameSession::discard_candidate() {
    candidate_preparation_.reset();
    candidate_.reset();
    pending_ = 0;
    progress_ = {};
}
GamePreparationProgress GameSession::poll_candidate() {
    try {
        if (candidate_preparation_) {
            auto next = candidate_preparation_->poll(*candidate_);
            if (next.completed > next.total || next.stage.empty() || next.stage.size() > 256)
                throw std::runtime_error("game.session: Invalid preparation progress");
            progress_ = std::move(next);
        }
        return progress_;
    } catch (const std::exception& e) {
        error_ = e.what();
        discard_candidate();
        throw;
    } catch (...) {
        error_ = "Non-standard native exception in scene preparation";
        discard_candidate();
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
    // Resume is post-commit. A device/native callback failure is a session fault,
    // never a claim that the old world has been restored.
    try {
        active_->simulation.audio_paused(!run);
        if (run)
            clock_.resume(now);
    } catch (const std::exception& e) {
        faulted_ = true;
        error_ = e.what();
        throw;
    }
}
void GameSession::cancel(std::uint64_t ticket) {
    owner();
    if (!candidate_ || ticket != pending_)
        throw std::runtime_error("game.session: Stale or missing prepared scene");
    Mutation guard(changing_);
    discard_candidate();
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
void GameSession::input(const std::vector<InputEvent>& events) {
    require_active();
    active_->simulation.input().submit(events);
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
