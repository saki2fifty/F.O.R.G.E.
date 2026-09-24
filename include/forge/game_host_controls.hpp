#pragma once
#include <forge/game_session.hpp>
#include <forge/game_settings.hpp>
#include <forge/game_storage.hpp>

namespace forge {
struct GamePlatformControls {
    std::function<void(const Json&)> settings;
    std::function<void(bool)> cursor;
    std::function<void(const std::string&)> navigation;
    std::function<std::string()> input_device;
};
// Reusable host executor. Game/menu policy lives in modules; this owner delegates
// copied requests to existing session, storage, input and narrow platform adapters.
class GameHostControls {
  public:
    GameHostControls(GameSession&, std::shared_ptr<GameControlQueue>, GameStorage&,
                     std::filesystem::path content, Json defaults, InputMap project_input,
                     Json user, GamePlatformControls = {});
    void pump(RuntimeClock::Time now);
    void release_cursor();
    void diagnostic(std::string text) {
        error_ = std::move(text).substr(0, 1024);
        publish();
    }
    bool cursor_captured() const { return cursor_; }
    bool quit_requested() const { return quit_; }
    const Json& settings() const { return settings_; }

  private:
    Json execute(const Json&, const GameSaveSchema*, const std::string&, RuntimeClock::Time);
    std::uint64_t prepare(AssetId, const Json&, bool activate, bool run);
    void apply_settings(Json user);
    void publish();
    GameSession& game_;
    std::shared_ptr<GameControlQueue> queue_;
    GameStorage& storage_;
    std::filesystem::path content_;
    Json defaults_, user_, settings_;
    InputMap project_input_;
    GamePlatformControls platform_;
    std::uint64_t auto_ticket_ = 0;
    bool auto_run_ = true, cursor_ = false, quit_ = false;
    std::optional<ActionId> rebind_action_;
    std::size_t rebind_index_ = 0;
    std::optional<InputEvent> rebind_candidate_;
    std::string error_;
};
} // namespace forge
