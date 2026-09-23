#pragma once
#include "frame_renderer.hpp"
#include <forge/game_session.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
namespace forge {
// One graphical host, one active scene and one unpublished candidate. This owner
// must outlive GameSession (including its preparation adapters). No editor linkage.
class GamePresentation {
  public:
    GamePresentation(DiligentPresentation&, Diligent::IDeviceContext*, std::filesystem::path,
                     std::vector<std::byte> font, UiPlatformCallbacks = {},
                     Rml::TextInputHandler* = nullptr);
    void dimensions(unsigned width, unsigned height, float density);
    std::unique_ptr<GameScenePreparation> prepare(std::uint64_t);
    Diligent::ITextureView* draw(GameSession&, double time);
    UiPresenter& ui() { return ui_; }
    const std::string& session() const { return session_; }
    std::uint64_t generation() const { return generation_; }

  private:
    struct Candidate;
    DiligentPresentation& presentation_;
    Diligent::IDeviceContext* context_;
    std::filesystem::path root_;
    std::shared_ptr<MeshResourceHost> resources_;
    UiDiligentRenderer ui_renderer_;
    UiPresenter ui_;
    std::shared_ptr<FrameRenderer> active_;
    std::string session_ = AssetId::generate().str(); // Ephemeral protocol correlation only.
    std::uint64_t generation_ = 0;
    unsigned width_ = 1280, height_ = 720;
    float density_ = 1;
};
} // namespace forge
