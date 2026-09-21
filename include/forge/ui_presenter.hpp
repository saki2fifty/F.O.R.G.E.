#pragma once
#include <forge/ui_assets.hpp>
#include <forge/ui_protocol.hpp>
#include <memory>
#include <optional>
namespace Rml {
class RenderInterface;
class TextInputHandler;
} // namespace Rml
namespace forge {
// Presentation host supplies graphics and already-admitted default font bytes.
// No editor, SDL, ImGui, WorldContext or gameplay library dependency.
struct UiPlatformCallbacks {
    std::function<std::string()> get_clipboard;
    std::function<void(const std::string&)> set_clipboard;
    std::function<void(float, float, float)> activate_text;
    std::function<void()> deactivate_text;
};
class UiPresenter {
  public:
    UiPresenter(Rml::RenderInterface&, std::filesystem::path project,
                std::vector<std::byte> default_font, UiPlatformCallbacks callbacks = {},
                Rml::TextInputHandler* text_handler = nullptr);
    ~UiPresenter();
    UiPresenter(const UiPresenter&) = delete;
    UiPresenter& operator=(const UiPresenter&) = delete;
    void reset(std::string session, std::uint64_t generation);
    bool accept(const nlohmann::json& snapshot);
    bool reload();
    void update(double elapsed_seconds, int width, int height, float density = 1);
    void render();
    bool mouse_move(int x, int y, int modifiers);
    bool mouse_button(int button, bool down, int modifiers);
    bool wheel(float x, float y, int modifiers);
    bool key(int key, bool down, int modifiers);
    bool text(const std::string& utf8);
    bool wants_text() const;
    void release_input();
    std::optional<nlohmann::json> pending_command() const;
    void acknowledge(const nlohmann::json& ack);
    const std::string& diagnostic() const;
    std::size_t document_count() const;
    std::uint64_t presentation_revision() const;
    // Last successfully published set only; borrowed until replacement/reset.
    const UiAssetSnapshot* asset_snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
