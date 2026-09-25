#include "game_presentation.hpp"
#include <forge/navigation.hpp>
namespace forge {
namespace {
std::shared_ptr<UiRuntime> runtime_ui(RuntimeWorld& world) {
    // RuntimeWorld installs these concrete built-in owners, as in the Play worker.
    // The Diligent Windows target disables RTTI; this is not plugin type discovery.
    auto ui = std::static_pointer_cast<UiRuntime>(world.engine.services().ui());
    if (!ui)
        throw std::runtime_error("Standalone runtime UI service is unavailable");
    return ui;
}
void admit_frame(const FrameRenderer& frame) {
    for (const auto& d : frame.diagnostics())
        if (d.severity == Severity::Error || d.severity == Severity::Fatal)
            throw std::runtime_error(d.category + ": " + d.text);
    if (frame.omitted_diagnostics())
        throw std::runtime_error("Scene preparation exceeded render diagnostic capacity");
}
} // namespace
struct GamePresentation::Candidate final : GameScenePreparation {
    GamePresentation& host;
    std::shared_ptr<FrameRenderer> frame;
    std::uint64_t generation, ui_ticket = 0;
    unsigned width = 0, height = 0;
    float density = 0;
    bool runtime_ready = false;
    Candidate(GamePresentation& h, std::uint64_t g)
        : host(h), frame(std::make_shared<FrameRenderer>(h.presentation_)), generation(g) {
        frame->resources(h.resources_);
    }
    ~Candidate() override {
        if (ui_ticket && host.ui_.prepared(ui_ticket))
            host.ui_.cancel_prepared(ui_ticket);
        if (host.active_ == frame) {
            host.active_.reset();
            host.ui_.reset({}, 0);
            host.generation_ = 0;
        }
    }
    GamePreparationProgress poll(RuntimeWorld& world) override {
        if (!runtime_ready) {
            if (!world.prepare_scene_resources())
                return {false, "animation", 0, 4};
            runtime_ready = true;
        }
        host.resources_->pump();
        frame->game(host.context_, world.simulation.presentation(1), host.width_, host.height_);
        host.resources_->submit();
        // Pending work may emit a temporary unavailable diagnostic. Wait for its
        // terminal result, then reject errors/fallbacks instead of activating them.
        if (frame->pending())
            return {false, "graphics", 1, 4};
        admit_frame(*frame);
        if (!ui_ticket || width != host.width_ || height != host.height_ ||
            density != host.density_) {
            if (ui_ticket && host.ui_.prepared(ui_ticket))
                host.ui_.cancel_prepared(ui_ticket);
            ui_ticket = 0;
            auto snapshot =
                runtime_ui(world)->snapshot(world.scene, host.session_, generation, 0, true);
            if (snapshot.contains("errors") && !snapshot.at("errors").empty())
                throw std::runtime_error("Scene preparation: " + snapshot.at("errors").dump());
            // Dimensions were already set by the host, including while loading.
            ui_ticket = host.ui_.prepare(snapshot);
            width = host.width_;
            height = host.height_;
            density = host.density_;
        }
        if (!host.ui_.prepared(ui_ticket))
            throw std::runtime_error("Scene preparation: UI candidate was superseded");
        return {true, "ready", 4, 4};
    }
    void activate() noexcept override {
        if (!host.ui_.activate_prepared(ui_ticket))
            std::terminate(); // Host owner invariant; never silently show a mismatched UI.
        ui_ticket = 0;
        host.active_ = frame;
        host.generation_ = generation;
    }
};
GamePresentation::GamePresentation(DiligentPresentation& p, Diligent::IDeviceContext* context,
                                   std::filesystem::path root, std::vector<std::byte> font,
                                   UiPlatformCallbacks callbacks, Rml::TextInputHandler* ime)
    : presentation_(p), context_(context), root_(std::move(root)),
      resources_(std::make_shared<MeshResourceHost>(p, context, root_)), ui_renderer_(p.device()),
      ui_(ui_renderer_.render_interface(), root_, std::move(font), std::move(callbacks), ime) {
    resources_->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root_)));
}
void GamePresentation::dimensions(unsigned width, unsigned height, float density) {
    width_ = width;
    height_ = height;
    density_ = density;
}
std::unique_ptr<GameScenePreparation> GamePresentation::prepare(std::uint64_t ticket) {
    return std::make_unique<Candidate>(*this, ticket);
}
Diligent::ITextureView* GamePresentation::draw(GameSession& game, double time) {
    ui_.loading(game.loading_state());
    ui_.update(time, int(width_), int(height_), density_);
    if (!active_)
        return nullptr;
    resources_->pump();
    auto* image = active_->game(context_, game.presentation(), width_, height_);
    resources_->submit();
    const auto status = game.status();
    ui_.accept(runtime_ui(game.active())
                   ->snapshot(game.active().scene, session_, generation_,
                              status.at("clock").at("tick"), status.at("state") == "paused"));
    ui_.update(time, int(width_), int(height_), density_);
    auto* target = image->GetTexture()->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    ui_renderer_.begin(context_, target, width_, height_);
    try {
        ui_.render();
    } catch (...) {
        ui_renderer_.end();
        throw;
    }
    ui_renderer_.end();
    return image;
}
} // namespace forge
