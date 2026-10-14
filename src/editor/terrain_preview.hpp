#pragma once
#include "camera.hpp"
#include "frame_renderer.hpp"
#include "render_projection.hpp"
#include "terrain_editor.hpp"
namespace forge {
// Detached source view: actual shared mesh renderer and picking, no Scene world
// mutation, source publication, temporary source writes or gameplay ticking.
class TerrainPreview {
  public:
    TerrainPreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                   std::filesystem::path project, AssetId asset)
        : context_(context), project_(std::move(project)), asset_(asset),
          host_(std::make_shared<MeshResourceHost>(presentation, context, project_, true)),
          frame_(presentation) {
        frame_.resources(host_);
        scene_.scene = AssetId::generate();
        scene_.settings.shadows.enabled = false;
        RenderMesh mesh;
        mesh.entity = EntityId::generate();
        mesh.renderer.mesh.id = asset_;
        mesh.legacy_tint = {{.32f, .58f, .26f}};
        scene_.meshes = {mesh};
        camera.yaw = .4f;
        camera.pitch = -.65f;
    }
    const std::filesystem::path& project() const { return project_; }
    AssetId asset() const { return asset_; }
    EditorCamera camera;
    Diligent::ITextureView* render(const TerrainSource& source, std::uint64_t revision,
                                   std::shared_ptr<const AssetCatalog> catalog, unsigned width,
                                   unsigned height) {
        if (source.asset() != asset_ || !revision || !width || !height)
            throw std::runtime_error("Terrain preview source/dimensions changed");
        host_->catalog(std::move(catalog));
        if (revision_ != revision) {
            auto mesh = source.mesh(); // Decode and validate the shared grid once.
            const auto side = source.document.at("side").get<unsigned>();
            const auto& tiles = source.document.at("tiles");
            std::map<TerrainTileId, std::vector<Double3>> outlines;
            for (std::size_t i = 0; i < tiles.size(); ++i) {
                const auto tile = TerrainTileId::parse(tiles[i].at("id").get<std::string>());
                const auto& positions =
                    std::get<std::vector<float>>(mesh.lods[0].parts[i].find("POSITION")->values);
                auto add = [&](unsigned x, unsigned z) {
                    const auto at = (std::size_t(z) * side + x) * 3;
                    outlines[tile].push_back({positions[at], positions[at + 1], positions[at + 2]});
                };
                for (unsigned x = 0; x + 1 < side; ++x)
                    add(x, 0);
                for (unsigned z = 0; z + 1 < side; ++z)
                    add(side - 1, z);
                for (unsigned x = side - 1; x > 0; --x)
                    add(x, side - 1);
                for (unsigned z = side - 1; z > 0; --z)
                    add(0, z);
            }
            host_->preview_mesh({asset_}, std::move(mesh));
            outlines_ = std::move(outlines);
            revision_ = revision;
        }
        Light light;
        light.intensity = 3;
        auto lighting = light_view(light, {});
        lighting.direction = {.4, -.8, .4};
        const auto length = std::sqrt(.96);
        for (auto& value : lighting.direction)
            value /= length;
        scene_.lights = {{light_id_, lighting}};
        auto view = [&] {
            Camera lens;
            lens.far_plane = 100000;
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
                    throw std::runtime_error("Terrain exceeds preview camera profile");
                image = frame_.render(context_, scene_, view(), width, height, 0);
                auto_frame_ = false;
            }
        host_->submit();
        return image;
    }
    std::optional<MeshBrushSample> draw(const TerrainSource& source, std::uint64_t revision,
                                        std::shared_ptr<const AssetCatalog> catalog,
                                        TerrainPreviewSettings settings) {
        if (ui::button("Frame terrain",
                       "Fit the whole terrain; camera controls stay in this source view."))
            auto_frame_ = true;
        FORGE_UI_PROBE("terrain:frame");
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(1.f, std::min(available.x, 1024.f)), 240 * ui::interface_scale};
        const auto width = unsigned(size.x), height = unsigned(size.y);
        auto* image = render(source, revision, std::move(catalog), width, height);
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(image)}, size);
        FORGE_UI_PROBE("terrain:preview_image");
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            orbiting_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            orbiting_ = false;
        if (orbiting_)
            camera.orbit(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
        if (hovered && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0) {
            camera.zoom(ImGui::GetIO().MouseWheel);
            auto_frame_ = false;
        }
        if (frame_.pending())
            ImGui::TextUnformatted("Preparing terrain preview resources...");
        for (const auto& error : frame_.diagnostics())
            ui::field_error(error.text);
        if (frame_.pending() || frame_.cameras().empty())
            return {};
        const auto& view = frame_.cameras().front().view;
        auto screen = [&](Double3 point) -> std::optional<ImVec2> {
            const auto projected = project_render_point(view, point);
            if (!projected)
                return {};
            return ImVec2{origin.x + float((*projected)[0] * size.x / width),
                          origin.y + float((*projected)[1] * size.y / height)};
        };
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        if (const auto selected = outlines_.find(settings.tile); selected != outlines_.end())
            for (std::size_t i = 0; i < selected->second.size(); ++i) {
                const auto a = screen(selected->second[i]),
                           b = screen(selected->second[(i + 1) % selected->second.size()]);
                if (a && b)
                    draw->AddLine(*a, *b, IM_COL32(255, 210, 65, 255), 2 * ui::interface_scale);
            }
        std::optional<MeshBrushSample> result;
        if (hovered && !orbiting_) {
            const auto mouse = ImGui::GetIO().MousePos;
            if (const auto point =
                    frame_.pick_point(scene_, view, (mouse.x - origin.x) * width / size.x,
                                      (mouse.y - origin.y) * height / size.y)) {
                result = MeshBrushSample{*point, 1};
                if (settings.brushing && std::isfinite(settings.radius) && settings.radius > 1e-4 &&
                    settings.radius <= 1000) {
                    constexpr unsigned steps = 48;
                    for (unsigned i = 0; i < steps; ++i) {
                        auto circle = [&](unsigned at) {
                            const auto angle = double(at) * 6.283185307179586 / steps;
                            return screen({(*point)[0] + settings.radius * std::cos(angle),
                                           (*point)[1],
                                           (*point)[2] + settings.radius * std::sin(angle)});
                        };
                        const auto a = circle(i), b = circle(i + 1);
                        if (a && b)
                            draw->AddLine(*a, *b, IM_COL32(255, 245, 180, 255),
                                          ui::interface_scale);
                    }
                }
            }
        }
        draw->PopClipRect();
        return result;
    }

  private:
    Diligent::IDeviceContext* context_;
    std::filesystem::path project_;
    AssetId asset_;
    std::shared_ptr<MeshResourceHost> host_;
    FrameRenderer frame_;
    RenderScene scene_;
    std::map<TerrainTileId, std::vector<Double3>> outlines_;
    std::uint64_t revision_ = 0;
    EntityId camera_id_ = EntityId::generate(), light_id_ = EntityId::generate();
    bool auto_frame_ = true, orbiting_ = false;
};
} // namespace forge
