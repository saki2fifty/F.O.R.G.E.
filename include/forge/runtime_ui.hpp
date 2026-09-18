#pragma once
#include <deque>
#include <forge/scene.hpp>
#include <forge/ui_components.hpp>
#include <forge/ui_protocol.hpp>
#include <forge/world.hpp>
#include <set>
#include <thread>
namespace forge {
EngineModule ui_module(std::filesystem::path project);
class UiRuntime final : public UiService {
  public:
    explicit UiRuntime(std::filesystem::path project, ServiceAccess services = {});
    void publish(EntityId, const std::string&, const Json&) override;
    void allow_action(const std::string&) override;
    std::optional<UiAction> poll_action(const std::string&) override;
    Json snapshot(const Scene&, const std::string& session, std::uint64_t generation,
                  std::uint64_t tick, bool paused);
    // Validates against the current scene, not a stale presentation snapshot.
    void command(const Scene&, const Json&, const std::function<void(const std::string&)>& control);
    void shutdown();

  private:
    void check() const;
    std::thread::id owner_ = std::this_thread::get_id();
    ServiceAccess services_;
    std::filesystem::path project_;
    struct Instance {
        AssetId asset;
        flecs::entity_t entity;
        std::string identity;
    };
    std::map<EntityId, Instance> instances_;
    std::uint64_t next_instance_ = 0;
    std::map<EntityId, Json> models_;
    std::set<std::string> actions_;
    std::vector<UiAction> pending_;
    std::uint64_t revision_ = 0;
    bool active_ = true;
};
} // namespace forge
