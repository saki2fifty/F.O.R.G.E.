#pragma once
#include <forge/engine_module.hpp>
#include <forge/game_control_service.hpp>
#include <functional>

namespace forge {
// Trusted host owner for scoped mailboxes. Contains no second session or storage
// implementation: the executor delegates to the existing owners.
class GameControlQueue {
  public:
    using Execute = std::function<nlohmann::json(const nlohmann::json&, const GameSaveSchema*,
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
    // At most the requests queued before this call; newly queued callback work
    // waits until the next host boundary. A scene switch invalidates old requests.
    void drain(const Execute&);

  private:
    struct State;
    struct Endpoint;
    std::shared_ptr<State> state_;
};
} // namespace forge
