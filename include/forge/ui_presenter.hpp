#pragma once
#include <array>
#include <cstddef>
#include <forge/loading_state.hpp>
#include <forge/ui_assets.hpp>
#include <forge/ui_protocol.hpp>
#include <functional>
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
// A host hit borrows the current rendered scene. The native presenter retains
// identities and normalized image coordinates, never a scene or geometry pointer.
struct UiSurfaceCallbacks {
    // A retained surface is supplied while an owned pointer press is active.
    std::function<std::optional<UiSurfaceHit>(int, int, std::optional<UiSurfaceHit>)> pick;
    // Project image coordinates back to host pixels, including native IME anchors.
    std::function<std::optional<std::array<double, 2>>(const UiSurfaceHit&)> project;
};
class UiPresenter {
  public:
    UiPresenter(Rml::RenderInterface&, std::filesystem::path project,
                std::vector<std::byte> default_font, UiPlatformCallbacks callbacks = {},
                Rml::TextInputHandler* text_handler = nullptr, bool shape_text = false,
                bool grapheme_editing = false);
    ~UiPresenter();
    UiPresenter(const UiPresenter&) = delete;
    UiPresenter& operator=(const UiPresenter&) = delete;
    // Authoring worker only: native DOM/styles without gameplay model evaluation.
    // This is the reliable static subset, not conditional-resource completeness.
    UiAssetSnapshot inspect_static(AssetRef<UiDocumentAsset>);
    // Exclusive editor use while no runtime/staged set is installed. A failed
    // candidate retains the previous preview; reset() retires either purpose.
    // refresh=false reuses an identical request, including its source failure;
    // true reloads saved dependencies. Layout includes the admitted source digest.
    bool preview_document(AssetRef<UiDocumentAsset>, const std::filesystem::path& source,
                          std::string_view text, int width, int height, float density = 1,
                          bool refresh = true);
    nlohmann::json preview_layout() const;
    std::string preview_hit(float x, float y) const;
    void reset(std::string session, std::uint64_t generation);
    bool accept(const nlohmann::json& snapshot);
    bool reload();
    // Presentation-only host values bound as forge_loading_* in every document.
    // Does not modify authored/gameplay models or their protocol budget.
    void loading(const LoadingState&);
    // Host presentation diagnostics only: bounded literal text, no input/model/source
    // authority. Empty hides it; disabled hosts create no diagnostic document.
    void frame_statistics(const std::string& text);
    std::optional<std::uint64_t> take_loading_cancel();
    // Explicit scene transition trust. Loads a second native context and its GPU
    // resources without replacing visible documents or routing candidate input.
    // One candidate; a new request retires the old candidate even on failure.
    std::uint64_t prepare(const nlohmann::json& snapshot);
    bool prepared(std::uint64_t ticket) const;
    // Owner-thread, nonthrowing publication of an already admitted candidate.
    // False means stale/wrong-thread; no resource loading occurs here.
    bool activate_prepared(std::uint64_t ticket) noexcept;
    void cancel_prepared(std::uint64_t ticket);
    // Late main-view drawing/resource faults remain diagnostics. Output methods
    // also throw after the native walk, so a producer cannot publish partial UI.
    void update(double elapsed_seconds, int width, int height, float density = 1);
    // The existing camera pass supplies its physical layout at the shared time.
    void update_output(AssetRef<RenderTextureAsset>, int width, int height, float density = 1);
    // Observations of the current document set, never output resource ownership.
    std::vector<AssetId> output_targets() const;
    bool has_output(AssetRef<RenderTextureAsset>) const;
    void render();
    void render_output(AssetRef<RenderTextureAsset>);
    // Transient owner-thread routing. Both callbacks or neither are required.
    // Replacing callbacks releases native input before borrowing the new host.
    void surface_input(UiSurfaceCallbacks);
    bool mouse_move(int x, int y, int modifiers);
    bool mouse_button(int button, bool down, int modifiers);
    bool wheel(float x, float y, int modifiers);
    bool key(int key, bool down, int modifiers);
    bool text(const std::string& utf8);
    bool wants_text() const;
    // Bounded native observations/actions; no DOM scripts or authored source/Scene writes.
    // Targets use the current document instance, unique element ID and presentation revision.
    nlohmann::json text_fields(const std::string& instance) const;
    nlohmann::json text_field_action(const std::string& instance, const std::string& element,
                                     std::uint64_t presentation_revision, const std::string& action,
                                     const std::string& text = {});
    nlohmann::json form_controls(const std::string& instance) const;
    nlohmann::json form_options(const std::string& instance, const std::string& element,
                                std::size_t offset = 0, std::size_t limit = 16) const;
    nlohmann::json form_control_action(const std::string& instance, const std::string& element,
                                       std::uint64_t presentation_revision,
                                       const std::string& action, bool checked = false,
                                       std::size_t option = 0,
                                       const std::string& option_state = {});
    nlohmann::json buttons(const std::string& instance) const;
    nlohmann::json press_button(const std::string& instance, const std::string& element,
                                std::uint64_t presentation_revision);
    nlohmann::json range_controls(const std::string& instance) const;
    nlohmann::json set_range_value(const std::string& instance, const std::string& element,
                                   std::uint64_t presentation_revision, double value);
    nlohmann::json widget_keyboard_action(const std::string& instance, const std::string& element,
                                          std::uint64_t presentation_revision,
                                          const std::string& kind, const std::string& action);
    // Semantic action routed by RuntimeInput; delegates to native RmlUi focus.
    void navigate(const std::string& direction);
    void release_input();
    // Semantic automation shares the presenter's declared-command queue/IDs.
    bool command(const std::string& instance, const std::string& name,
                 const std::optional<std::string>& value = {});
    std::optional<nlohmann::json> pending_command() const;
    void acknowledge(const nlohmann::json& ack);
    const std::string& diagnostic() const;
    std::size_t document_count() const;
    std::uint64_t presentation_revision() const;
    // Last successfully published set only; borrowed until replacement/reset.
    const UiAssetSnapshot* asset_snapshot() const;
    // Actual displayed catalog/locale and fallback coverage; null when unconfigured.
    nlohmann::json localization_state() const;
    // Copied observations from the displayed set; null when unconfigured.
    nlohmann::json application_states_state() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
