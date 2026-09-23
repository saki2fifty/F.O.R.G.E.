#pragma once
#include <forge/identity.hpp>
#include <forge/project_lease.hpp>
#include <functional>
#include <thread>
namespace forge {
struct GameSave {
    AssetId scene;
    nlohmann::json data; // Only the game's explicitly admitted persistent state.
};
struct GameSaveSchema {
    unsigned version = 1;
    // Required even at version1: game code owns fields/references/content compatibility.
    std::function<void(const GameSave&)> validate;
    // Key N upgrades N -> N+1. Pure value transforms: no world/file side effects.
    std::map<unsigned, std::function<GameSave(GameSave)>> migrations;
};
// No editor or platform UI dependency. The host supplies the OS user-data base,
// never the install/project/cache directory. One cooperative writer per application.
class GameStorage {
  public:
    GameStorage(std::filesystem::path user_base, std::string application_id);
    void save(std::string_view slot, const GameSave&, const GameSaveSchema&);
    GameSave load(std::string_view slot, const GameSaveSchema&) const;
    std::vector<std::string> slots() const;
    void erase(std::string_view slot);
    void save_settings(const nlohmann::json&,
                       const std::function<void(const nlohmann::json&)>& validate);
    nlohmann::json load_settings(const std::function<void(const nlohmann::json&)>& validate) const;
    const std::filesystem::path& root() const { return root_; }

  private:
    void check() const;
    std::filesystem::path slot_path(std::string_view) const;
    std::filesystem::path root_;
    std::string application_;
    ProjectLease lease_;
    std::thread::id owner_ = std::this_thread::get_id();
};
} // namespace forge
