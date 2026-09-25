#pragma once
#include <forge/engine_module.hpp>
#include <forge/game_control_service.hpp>
#include <functional>
#include <optional>

namespace forge {
// Trusted host owner for scoped mailboxes. Contains no second session or storage
// implementation: the executor delegates to the existing owners.
class GameControlQueue {
  public:
    using Execute = std::function<nlohmann::json(const nlohmann::json&, const GameSaveSchema*,
                                                 const std::string& module)>;
    // Poll variant: callback receives the request token alongside the same
    // command/schema/module arguments. Return nullopt to keep the entry
    // pending (requeued behind any newly queued work); return a Json to
    // complete (an explicit Json null is a successful null result distinct
    // from nullopt); throw to fail. Each pending entry runs at most once
    // per host call; world replacement, module revocation, and explicit
    // request release invalidate pending work. The 256-entry queue bound
    // reserves capacity for the in-flight callback.
    using Poll = std::function<std::optional<nlohmann::json>(
        std::uint64_t token, const nlohmann::json&, const GameSaveSchema*,
        const std::string& module)>;
    GameControlQueue();
    ~GameControlQueue();
    GameControlQueue(const GameControlQueue&) = delete;
    GameControlQueue& operator=(const GameControlQueue&) = delete;
    std::shared_ptr<GameControlService> world();
    EngineModule module();
    void activate(const std::shared_ptr<GameControlService>&);
    void retire(const std::shared_ptr<GameControlService>&);
    void status(nlohmann::json copied);
    // True when a token is still pending (queued or in-flight) under the
    // currently active endpoint. Owner-thread read-only query used by the
    // SDK Play host to keep its platform-effect offers consistent with
    // the actual queue lifetime without duplicating command authority.
    bool pending(std::uint64_t token) const;
    // At most the requests queued before this call; newly queued callback work
    // waits until the next host boundary. A scene switch invalidates old requests.
    void drain(const Execute&);
    // Polling variant. Behaves like drain but allows callbacks to defer
    // completion; entries stay queued until they return a Json value or fail.
    void poll(const Poll&);

  private:
    struct State;
    struct Endpoint;
    std::shared_ptr<State> state_;
};
} // namespace forge
