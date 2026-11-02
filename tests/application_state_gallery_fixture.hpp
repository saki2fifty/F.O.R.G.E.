#pragma once
#include <chrono>
#include <forge/application_states.hpp>
#include <forge/game_session.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <thread>

namespace forge::test_state_gallery {
inline constexpr const char* scene_path = "Scenes/134-signal-lock.scene.json";
inline constexpr const char* state_path = "Assets/UI/SignalLock/SignalLock.appstates.json";
inline Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
inline void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
class Game {
    GameSession session_;
    std::shared_ptr<UiRuntime> ui_;
    std::uint64_t ticket_;
    static GameSessionConfig config(const std::filesystem::path& root, bool audio = false) {
        GameSessionConfig value;
        value.content_root = root;
        value.ui = true;
        if (audio)
            value.audio = AudioConfig{root, AudioOutput::Offline, true};
        return value;
    }

  public:
    explicit Game(const std::filesystem::path& root) : Game(root, read(root / scene_path)) {}
    Game(const std::filesystem::path& root, const Json& source, bool audio = false)
        : session_(config(root, audio)) {
        ticket_ = session_.prepare(source);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!session_.poll_preparation(ticket_).ready) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Signal Lock state preparation timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        session_.activate(ticket_, {}, false);
        ui_ = std::static_pointer_cast<UiRuntime>(session_.active().engine.services().ui());
    }
    Json snapshot() {
        return ui_->snapshot(session_.active().scene, "signal-lock", ticket_,
                             session_.status().at("clock").at("tick"),
                             session_.status().at("clock").at("paused"));
    }
    void command(const Json& request) {
        ui_->command(
            session_.active().scene, request,
            [&](const std::string& name) {
                if (name == "Pause")
                    session_.pause({});
                else if (name == "Resume")
                    session_.resume({});
                else if (name == "Step")
                    session_.step();
                else
                    throw std::runtime_error("Unexpected Signal Lock clock command");
            },
            [&](double scale) { session_.set_time_scale(scale); });
    }
    const Scene& scene() { return session_.active().scene; }
    GameSession& session() { return session_; }
};
} // namespace forge::test_state_gallery
