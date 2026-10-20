#pragma once
#include "../frame_renderer.hpp"
#include "../mesh_render_host.hpp"
#include "../tile_map_data.hpp"
#include "../tile_map_pick.hpp"
#include "../tile_set_data.hpp"
#include "editor_state.hpp"
#include "widgets.hpp"
#include <numeric>

namespace forge {
// One asset document's detached XY canvas. The document/gesture supplies immutable
// values; this owner only renders, navigates and reports a grid pointer sample.
class TileMapPreview {
  public:
    TileMapPreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                   std::filesystem::path project, AssetId map, AssetId definitions)
        : context_(context), project_(std::move(project)), map_(map), definitions_(definitions),
          host_(std::make_shared<MeshResourceHost>(presentation, context, project_, true)),
          frame_(presentation, "Tile preview") {
        frame_.resources(host_);
        scene_.scene = AssetId::generate();
        scene_.settings.shadows.enabled = false;
        RenderTileMap actor;
        actor.entity = EntityId::generate();
        actor.renderer.map.id = map_;
        scene_.tile_maps.push_back(actor);
    }
    const std::filesystem::path& project() const { return project_; }
    AssetId asset() const { return map_; }
    AssetId definitions() const { return definitions_; }
    bool pending() const { return frame_.pending(); }
    const std::vector<Diagnostic>& diagnostics() const { return frame_.diagnostics(); }
    void frame_cells() { fit_ = true; }
    Diligent::ITextureView* render(std::shared_ptr<const TileMapData> cells,
                                   std::shared_ptr<const TileSetData> definitions,
                                   std::shared_ptr<const AssetCatalog> catalog, unsigned width,
                                   unsigned height) {
        if (!cells || cells->asset() != map_ || cells->tile_set() != definitions_ ||
            (definitions && definitions->asset() != definitions_) || !width || !height)
            throw std::runtime_error("Tile preview values or dimensions changed");
        host_->catalog(std::move(catalog));
        host_->preview_tile_map({map_}, cells);
        if (definitions)
            host_->preview_tile_set({definitions_}, definitions);
        // Preserve camera position on edits; fit only on first use or request.
        if (fit_) {
            std::optional<RenderBounds> bounds;
            const auto size = cells->cell_size();
            cells->for_each_chunk([&](auto key, auto values, auto) {
                for (std::uint32_t slot = 0; slot < tile_chunk_cells; ++slot) {
                    if (!values[slot].tile)
                        continue;
                    const auto at = tile_cell_address({key.address, slot});
                    const RenderBounds box{
                        {double(at.x) * size[0], double(at.y) * size[1], 0},
                        {(double(at.x) + 1) * size[0], (double(at.y) + 1) * size[1], 0}};
                    if (!bounds)
                        bounds = box;
                    else
                        for (unsigned axis = 0; axis < 2; ++axis) {
                            bounds->minimum[axis] =
                                std::min(bounds->minimum[axis], box.minimum[axis]);
                            bounds->maximum[axis] =
                                std::max(bounds->maximum[axis], box.maximum[axis]);
                        }
                }
            });
            if (bounds) {
                for (unsigned axis = 0; axis < 2; ++axis)
                    center_[axis] = std::midpoint(bounds->minimum[axis], bounds->maximum[axis]);
                height_ = std::max(bounds->maximum[1] - bounds->minimum[1],
                                   (bounds->maximum[0] - bounds->minimum[0]) * height / width) *
                          1.2;
            } else {
                center_ = {};
                height_ = size[1] * 16;
            }
            height_ = std::clamp(height_, 1e-7, 1e16);
            fit_ = false;
        }
        Camera lens;
        lens.projection = unsigned(CameraProjection::Orthographic);
        lens.basis = unsigned(ViewBasis::GltfNegativeZ);
        lens.orthographic_height = height_;
        lens.near_plane = .01;
        lens.far_plane = 10;
        AffineTransform camera;
        // Existing negative-Z basis views the Quad front with X right and Y up.
        camera.m[3] = center_[0];
        camera.m[7] = center_[1];
        camera.m[11] = 4;
        view_ = camera_view(lens, camera, width, height);
        const std::array views{PreparedCamera{camera_id_, lens, view_}};
        auto* image = frame_.render(context_, scene_, views, width, height, 0);
        host_->submit();
        return image;
    }
    std::optional<TileCellAddress> draw(std::shared_ptr<const TileMapData> cells,
                                        std::shared_ptr<const TileSetData> definitions,
                                        std::shared_ptr<const AssetCatalog> catalog,
                                        bool stroke_active, bool painting,
                                        float minimum_height = 220) {
        ImGui::BeginDisabled(stroke_active);
        if (ui::button("Frame cells", "Fit occupied cells; middle mouse pans and wheel zooms."))
            frame_cells();
        FORGE_UI_PROBE("tile_map:frame");
        ImGui::EndDisabled();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(1.f, available.x),
                          std::max(minimum_height * ui::interface_scale, available.y)};
        const float scale = std::min(1.f, 4096.f / std::max(size.x, size.y));
        const auto width = std::max(1u, unsigned(size.x * scale)),
                   height = std::max(1u, unsigned(size.y * scale));
        auto* image = render(cells, definitions, std::move(catalog), width, height);
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(image)}, size);
        FORGE_UI_PROBE("tile_map:preview_image");
        const bool hovered = ImGui::IsItemHovered();
        auto& io = ImGui::GetIO();
        const bool captured =
            stroke_active || (painting && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        if (!captured && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            panning_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || captured)
            panning_ = false;
        if (panning_) {
            // Drag the canvas with the pointer; screen Y increases downward.
            center_[0] -= io.MouseDelta.x * height_ / size.y;
            center_[1] += io.MouseDelta.y * height_ / size.y;
        }
        if (!captured && hovered && !io.KeyCtrl && io.MouseWheel != 0)
            height_ = std::clamp(height_ * std::exp(-double(io.MouseWheel) * .15), 1e-7, 1e16);
        if (frame_.pending())
            ImGui::TextUnformatted("Preparing tile preview...");
        for (const auto& error : frame_.diagnostics())
            ui::field_error(error.text);
        // Resource errors do not invalidate the authored plane. Erase/replace
        // must remain possible when a tile definition or Material is missing.
        if (!hovered || !painting || panning_)
            return {};
        const auto hit = pick_tile_grid(cells->cell_size(), {}, view_,
                                        (io.MousePos.x - origin.x) * width / size.x,
                                        (io.MousePos.y - origin.y) * height / size.y);
        if (!hit)
            return {};
        const auto cell = hit->cell;
        const auto grid = cells->cell_size();
        const std::array<Double3, 4> corners{
            {{double(cell.x) * grid[0], double(cell.y) * grid[1], 0},
             {(double(cell.x) + 1) * grid[0], double(cell.y) * grid[1], 0},
             {(double(cell.x) + 1) * grid[0], (double(cell.y) + 1) * grid[1], 0},
             {double(cell.x) * grid[0], (double(cell.y) + 1) * grid[1], 0}}};
        auto* overlay = ImGui::GetWindowDrawList();
        overlay->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        for (unsigned i = 0; i < 4; ++i) {
            const auto a = project_render_point(view_, corners[i]),
                       b = project_render_point(view_, corners[(i + 1) % 4]);
            if (a && b)
                overlay->AddLine({origin.x + float((*a)[0]) * size.x / width,
                                  origin.y + float((*a)[1]) * size.y / height},
                                 {origin.x + float((*b)[0]) * size.x / width,
                                  origin.y + float((*b)[1]) * size.y / height},
                                 IM_COL32(255, 205, 80, 255), 1.5f);
        }
        overlay->PopClipRect();
        return cell;
    }

  private:
    Diligent::IDeviceContext* context_;
    std::filesystem::path project_;
    AssetId map_, definitions_;
    EntityId camera_id_ = EntityId::generate();
    std::shared_ptr<MeshResourceHost> host_;
    FrameRenderer frame_;
    RenderScene scene_;
    CameraView view_;
    std::array<double, 2> center_{};
    double height_ = 16;
    bool fit_ = true, panning_ = false;
};
} // namespace forge
