#pragma once
#include "camera.hpp"
#include "frame_renderer.hpp"
#include "property_drawer.hpp"
#include <forge/particle_effect_source.hpp>
namespace forge {
// Detached visual document, using the actual shared frame/mesh renderer.
// No Scene world, gameplay, source publication or replacement material renderer.
class ParticleEffectPreview {
  public:
    ParticleEffectPreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                          std::filesystem::path project, AssetId effect)
        : context_(context), project_(std::move(project)), effect_(effect),
          host_(std::make_shared<MeshResourceHost>(presentation, context, project_, true)),
          frame_(presentation) {
        frame_.resources(host_);
        scene_.scene = AssetId::generate();
        scene_.settings.shadows.enabled = false;
        emitter_.entity = EntityId::generate();
        emitter_.emitter.effect.id = effect_;
        camera.target = {0, 0, 0};
        camera.yaw = -.5f;
        camera.pitch = -.25f;
    }
    const std::filesystem::path& project() const { return project_; }
    AssetId effect() const { return effect_; }
    EditorCamera camera;
    Diligent::ITextureView* render(const ParticleEffectSource& source, double time,
                                   std::shared_ptr<const AssetCatalog> catalog, unsigned width,
                                   unsigned height) {
        if (source.asset() != effect_ || !width || !height)
            throw std::runtime_error("Particle preview source/dimensions changed");
        host_->catalog(std::move(catalog));
        host_->preview_particle_effect({effect_}, source.compiled());
        emitter_.time = time;
        scene_.particles = {emitter_};
        Light light;
        light.intensity = 3;
        auto lighting = light_view(light, {});
        lighting.direction = {.4, -.8, .4};
        const auto length = std::sqrt(.16 + .64 + .16);
        for (auto& value : lighting.direction)
            value /= length;
        scene_.lights = {{light_id_, lighting}};
        auto view = [&] {
            Camera lens;
            lens.far_plane = 10000;
            AffineTransform world;
            const auto right = camera.right(), up = camera.up(), forward = camera.forward(),
                       eye = camera.eye();
            for (unsigned axis = 0; axis < 3; ++axis) {
                world.m[axis * 4] = right[axis];
                world.m[axis * 4 + 1] = up[axis];
                world.m[axis * 4 + 2] = forward[axis];
                world.m[axis * 4 + 3] = eye[axis];
            }
            return std::array{
                PreparedCamera{camera_id_, lens, camera_view(lens, world, width, height)}};
        };
        auto* image = frame_.render(context_, scene_, view(), width, height, 0);
        if (!frame_.pending() && frame_.diagnostics().empty() && auto_frame_)
            if (const auto bounds = frame_.bounds()) {
                EditorCamera::Vec center;
                double radius = 0;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    center[axis] = float(bounds->minimum[axis] * .5 + bounds->maximum[axis] * .5);
                    const auto half = (bounds->maximum[axis] - bounds->minimum[axis]) * .5;
                    radius += half * half;
                }
                if (!camera.frame_sphere(center, std::sqrt(radius), float(width) / height))
                    throw std::runtime_error(
                        "Particle envelope exceeds the preview camera profile");
                image = frame_.render(context_, scene_, view(), width, height, 0);
                auto_frame_ = false;
            }
        host_->submit();
        return image;
    }
    void draw(const ParticleEffectSource& source, double time,
              std::shared_ptr<const AssetCatalog> catalog) {
        if (ui::button("Frame effect",
                       "Fit the lifetime envelope; orbit and zoom remain local to this preview."))
            auto_frame_ = true;
        FORGE_UI_PROBE("particles:frame");
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(1.f, std::min(available.x, 1024.f)),
                          std::min(240 * ui::interface_scale, std::max(1.f, available.y))};
        try {
            auto* image =
                render(source, time, std::move(catalog), unsigned(size.x), unsigned(size.y));
            ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(image)}, size);
            FORGE_UI_PROBE("particles:preview_image");
            const bool hovered = ImGui::IsItemHovered();
            ui::help("Middle mouse orbits; wheel zooms this preview. Scene, Play and exports share "
                     "the effect evaluator and mesh instancing.");
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                orbiting_ = true;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                orbiting_ = false;
            if (orbiting_)
                camera.orbit(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
            if (hovered && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0) {
                auto_frame_ = false;
                camera.zoom(ImGui::GetIO().MouseWheel);
            }
            if (frame_.pending())
                ImGui::TextUnformatted("Preparing particle preview resources...");
            for (const auto& error : frame_.diagnostics())
                ui::field_error(error.text);
        } catch (const std::exception& error) {
            ui::field_error(error.what());
        }
    }

  private:
    Diligent::IDeviceContext* context_;
    std::filesystem::path project_;
    AssetId effect_;
    std::shared_ptr<MeshResourceHost> host_;
    FrameRenderer frame_;
    RenderScene scene_;
    RenderParticleEmitter emitter_;
    EntityId camera_id_ = EntityId::generate(), light_id_ = EntityId::generate();
    bool auto_frame_ = true, orbiting_ = false;
};
} // namespace forge
