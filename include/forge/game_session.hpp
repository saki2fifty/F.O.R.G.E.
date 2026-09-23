#pragma once
#include <forge/loading_state.hpp>
#include <forge/runtime_world.hpp>
namespace forge {
struct GamePreparationProgress {
    bool ready = false;
    std::string stage;
    std::size_t completed = 0, total = 0;
};
// Trusted host adapter, owned by the session. poll may prepare resources but must
// not publish presentation or tick gameplay. Throws reject only the candidate.
// activate only publishes already prepared ownership; it must not allocate/load.
class GameScenePreparation {
  public:
    virtual ~GameScenePreparation() = default;
    virtual GamePreparationProgress poll(RuntimeWorld&) = 0;
    virtual void activate() noexcept = 0;
};
struct GameSessionConfig {
    RuntimeConfig clock;
    InputMap input{};
    PhysicsConfig physics;
    std::optional<AudioConfig> audio;
    std::filesystem::path content_root;
    bool ui = false;
    std::vector<EngineModule> modules;
    std::function<std::unique_ptr<GameScenePreparation>(std::uint64_t)> preparation;
};
// Owner-thread simulation session. Presentation/window and persistent saves remain
// separate owners. One active scene and at most one unpublished candidate world.
class GameSession {
  public:
    explicit GameSession(GameSessionConfig);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    // Synchronous structural/physics preparation. This is NOT an async asset/GPU
    // readiness promise. The visual host must admit required resources before activate.
    // Optional game-owned restoration mutates only the unpublished Scene. It runs
    // before physics realization; throwing discards the candidate. No generic ECS dump.
    std::uint64_t prepare(const Json& snapshot,
                          const std::function<void(Scene&)>& restore_supported_state = {});
    // Nonblocking host work is polled without advancing either world's clock.
    // An installed preparation adapter is mandatory: activate polls it again and
    // refuses publication until ready. Without one, scope remains structural only.
    GamePreparationProgress poll_preparation(std::uint64_t ticket);
    void activate(std::uint64_t ticket, RuntimeClock::Time now, bool run);
    void cancel(std::uint64_t ticket);
    void unload(RuntimeClock::Time now);
    void pause(RuntimeClock::Time now);
    void resume(RuntimeClock::Time now);
    void step();
    void advance(RuntimeClock::Time now);
    void input(const std::vector<InputEvent>&);
    Json status() const;
    LoadingState loading_state() const;
    Json presentation() const;
    RuntimeWorld& active(); // Borrowed until activation/unload; never persisted.

  private:
    void owner() const;
    void require_active() const;
    void tick(float dt);
    GamePreparationProgress poll_candidate();
    void discard_candidate();
    GameSessionConfig config_;
    Module module_; // Last runtime code lease released after all worlds.
    RuntimeClock clock_;
    std::unique_ptr<RuntimeWorld> active_, candidate_;
    // Destroy resource consumers before the worlds/code they may refer to.
    std::unique_ptr<GameScenePreparation> active_preparation_, candidate_preparation_;
    GamePreparationProgress progress_;
    LoadingState loading_;
    std::thread::id thread_ = std::this_thread::get_id();
    std::uint64_t generation_ = 0, pending_ = 0;
    bool faulted_ = false, changing_ = false;
    std::string error_;
};
} // namespace forge
