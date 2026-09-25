#pragma once
#include <forge/game_session.hpp>
#include <forge/game_settings.hpp>
#include <forge/game_storage.hpp>
#include <optional>

namespace forge {
struct GamePlatformControls {
    std::function<void(const Json&)> settings;
    std::function<void(bool)> cursor;
    std::function<void(const std::string&)> navigation;
    std::function<std::string()> input_device;
    // Optional polled cursor capture adapter. Takes the request token plus
    // the desired capture state and returns true once the physical cursor
    // effect has actually completed; false while the effect is still in
    // flight in the editor/native side; throw to fail. The adapter owns
    // idempotency: it must dispatch the physical effect at most once per
    // token, and must throw for tokens that have been released or
    // cancelled (including by the synchronous release_cursor path).
    // false alone means "still legitimately pending", NOT "cancelled".
    // The engine never falsely claims that a delayed physical effect is
    // cancelled merely because a queue receipt was dropped. No production
    // IPC claim — this is host C++ API only and adapters must be supplied
    // by the embedding shell.
    std::function<bool(std::uint64_t token, bool desired)> cursor_poll;
    // Optional polled UI navigation adapter. Same contract as cursor_poll
    // but takes the desired direction string. Navigation-only adapters do
    // NOT require a synchronous cursor callback because they do not own
    // capture/release; only cursor_poll does.
    std::function<bool(std::uint64_t token, const std::string& direction)> navigation_poll;
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
    std::optional<Json> execute(std::uint64_t token, const Json&, const GameSaveSchema*,
                                const std::string&, RuntimeClock::Time);
    // Poll the configured polled cursor adapter for a release effect against
    // the given request token. Returns true on ack (or with no polled
    // adapter), false while the effect is still in flight to requeue, or
    // throws to fail the request. The helper does NOT call release_cursor;
    // callers do exactly once on a true return. Does not skip on !cursor_.
    bool poll_cursor_release(std::uint64_t token);
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
