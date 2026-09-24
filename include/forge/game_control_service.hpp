#pragma once
#include <forge/game_storage.hpp>
#include <memory>

namespace forge {
// World-scoped mailbox. Requests contain copied values, never a GameSession,
// Scene, window, device or filesystem pointer. A host drains after callbacks.
struct GameRequestStatus {
    std::string state; // queued, succeeded, failed
    nlohmann::json value = nlohmann::json::object();
    std::string error;
};
class GameControlService {
  public:
    virtual ~GameControlService() = default;
    virtual nlohmann::json query() const = 0;
    virtual std::uint64_t request(const std::string& module, const nlohmann::json& command) = 0;
    virtual GameRequestStatus inspect(const std::string& module, std::uint64_t token) const = 0;
    virtual void release(const std::string& module, std::uint64_t token) = 0;
    // The registration explicitly retains its native code lease until all
    // synchronous validation/migration callbacks return. Revocation is mandatory.
    virtual void save_schema(const std::string& module, GameSaveSchema,
                             std::shared_ptr<void> code) = 0;
    virtual void revoke(const std::string& module) = 0;
};
} // namespace forge
