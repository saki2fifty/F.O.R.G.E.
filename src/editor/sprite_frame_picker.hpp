#pragma once
#include "sprite_atlas_frames.hpp"
#include "sprite_frame_context.hpp"
#include "sprite_sheet_layout.hpp"
#include "widgets.hpp"
#include <forge/resource.hpp>
#include <functional>
#include <imgui_internal.h>
#include <string_view>

namespace forge::ui {
struct SpriteSheetImage {
    std::uint32_t width = 0, height = 0;
    std::optional<ResourceIdentity> revision;
    std::string status;
    std::function<ImTextureID(unsigned, unsigned, std::array<float, 4>)> render;
    std::function<void()> retry;
    bool ready() const { return width && height && revision && bool(render); }
};
// One transient chooser. Call begin_frame/after_ui around its ordinary field
// consumers; publication remains in each caller's existing property/key owner.
class SpriteFramePicker {
  public:
    std::function<SpriteSheetImage(AssetRef<MaterialAsset>)> image_source;
    std::optional<nlohmann::json> paste_atlas_frames(const SpriteFrameContext& context) {
        std::optional<nlohmann::json> result;
        if (button("Paste atlas frames", "Replace frame rectangles from Copy atlas frames. "
                                         "Uses the assigned material's Texture; resets the frame "
                                         "to 0 and disables cropping.")) {
            paste_owner_ = context.owner;
            try {
                const auto* clipboard = ImGui::GetClipboardText();
                if (!clipboard)
                    throw std::invalid_argument("Copy an atlas frame layout first");
                std::size_t size = 0;
                while (size <= 65536 && clipboard[size])
                    ++size;
                if (size > 65536)
                    throw std::invalid_argument("Atlas clipboard exceeds 64 KiB");
                const auto image =
                    image_source ? image_source(context.material) : SpriteSheetImage{};
                if (!image.ready())
                    throw std::runtime_error(
                        image.status.empty()
                            ? "Wait for the assigned Sprite image to load, then paste again"
                            : image.status);
                result = sprite_atlas_frame_values({clipboard, size}, image.revision->asset,
                                                   image.width, image.height);
                paste_error_.clear();
            } catch (const std::exception& error) {
                paste_error_ = error.what();
            }
        }
        FORGE_UI_PROBE("sprite:paste-atlas");
        if (paste_owner_ == context.owner && !paste_error_.empty())
            field_error(paste_error_);
        return result;
    }
    void begin_frame() {
        submitted_ = false;
#ifdef FORGE_UI_FIXTURE
        draw_calls_ = 0;
#endif
    }
    void after_ui() {
        if (popup_ && !submitted_) {
#ifdef FORGE_UI_FIXTURE
            if (close_reason_.empty())
                close_reason_ = draw_calls_ ? "popup no longer submitted" : "field not drawn";
#endif
            cancel();
        }
    }
#ifdef FORGE_UI_FIXTURE
    nlohmann::json diagnostics() const {
        const auto* g = ImGui::GetCurrentContext();
        return {{"close_reason", close_reason_},
                {"message", message_},
                {"submitted", submitted_},
                {"draw_calls", draw_calls_},
                {"popup_id", popup_},
                {"owner", owner_},
                {"pending", pending_},
                {"canvas_focused", canvas_focused_},
                {"nav_window", g && g->NavWindow ? g->NavWindow->Name : ""},
                {"nav_id", g ? g->NavId : 0}};
    }
#endif
    bool open() const {
        return ImGui::GetCurrentContext() && popup_ &&
               ImGui::IsPopupOpen(popup_, ImGuiPopupFlags_None);
    }
    void cancel() {
        if (auto* context = ImGui::GetCurrentContext(); context && popup_) {
            auto& stack = context->OpenPopupStack;
            for (int i = 0; i < stack.Size; ++i)
                if (stack[i].PopupId == popup_) {
                    ImGui::ClosePopupToLevel(i, true);
                    break;
                }
        }
        popup_ = 0;
        image_revision_.reset();
    }
    bool draw(const SpriteFrameContext& context, std::uint32_t& value) {
#ifdef FORGE_UI_FIXTURE
        ++draw_calls_;
#endif
        if (button("Choose frame...", "See the image sheet and choose a whole frame.")) {
            owner_ = context.owner;
            material_ = context.material;
            grid_ = grid_key(context.grid);
            crop_ = context.crop;
            frame_regions_ = context.frame_regions;
            pending_ = value;
            view_ = {};
            image_revision_.reset();
            message_.clear();
#ifdef FORGE_UI_FIXTURE
            close_reason_.clear();
#endif
            popup_ = ImGui::GetID("Sprite frames");
            ImGui::OpenPopup("Sprite frames");
        }
        FORGE_UI_PROBE("sprite:choose-frame");
        if ((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) &&
            ImGui::IsPopupOpen("Sprite frames")) {
#ifdef FORGE_UI_FIXTURE
            close_reason_ = "field disabled";
#endif
            cancel();
            return false;
        }
        if (!message_.empty() && owner_ == context.owner)
            ImGui::TextWrapped("%s", message_.c_str());
        if (!ImGui::IsPopupOpen("Sprite frames"))
            return false;
        submitted_ = true;
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize({std::min(900.f * interface_scale, viewport->WorkSize.x * .9f),
                                  std::min(650.f * interface_scale, viewport->WorkSize.y * .9f)},
                                 ImGuiCond_Always);
        if (!ImGui::BeginPopup("Sprite frames", ImGuiWindowFlags_NoSavedSettings))
            return false;
        bool chosen = false;
        if (owner_ != context.owner || material_ != context.material ||
            grid_ != grid_key(context.grid) || crop_ != context.crop ||
            frame_regions_ != context.frame_regions) {
#ifdef FORGE_UI_FIXTURE
            close_reason_ = "owner, material, grid or region changed";
#endif
            message_ = "The Sprite, frames or image region changed. Open Choose frame again.";
            ImGui::CloseCurrentPopup();
        } else {
            chosen = contents(context, value);
        }
        ImGui::EndPopup();
        return chosen;
    }

  private:
    static std::array<std::uint32_t, 6> grid_key(const SpriteGridLayout& grid) {
        return {grid.columns,  grid.rows,      grid.margin_x,
                grid.margin_y, grid.spacing_x, grid.spacing_y};
    }
    void frame_lines(const SpriteSheetViewport& view, const SpriteGridLayout& grid,
                     const SpritePixelRegion& cell, ImVec2 origin,
                     std::optional<SpritePixelRegion> crop,
                     std::span<const SpriteFrameRegion> regions) {
        auto* draw = ImGui::GetWindowDrawList();
        const double starts[] = {view.region[0] * view.texture_width,
                                 view.region[1] * view.texture_height};
        const double ends[] = {view.region[2] * view.texture_width,
                               view.region[3] * view.texture_height};
        const double scales[] = {view.width / (ends[0] - starts[0]),
                                 view.height / (ends[1] - starts[1])};
        if (!regions.empty()) {
            const auto area =
                sprite_grid_region(view.texture_width, view.texture_height, {}, 0, crop);
            for (const auto& region : regions) {
                const double x = double(area.x) + region.x, y = double(area.y) + region.y;
                draw->AddRect({origin.x + float((x - starts[0]) * scales[0]),
                               origin.y + float((y - starts[1]) * scales[1])},
                              {origin.x + float((x + region.width - starts[0]) * scales[0]),
                               origin.y + float((y + region.height - starts[1]) * scales[1])},
                              IM_COL32(220, 225, 235, 115));
            }
        }
        for (unsigned axis = 0; regions.empty() && axis < 2; ++axis) {
            const auto margin = axis ? cell.y : cell.x;
            const auto cells = axis ? grid.rows : grid.columns;
            const auto extent = axis ? cell.height : cell.width;
            const auto pitch = std::uint64_t(extent) + (axis ? grid.spacing_y : grid.spacing_x);
            if (extent * scales[axis] < 8 * interface_scale)
                continue;
            const auto first =
                std::uint64_t(std::max(0., std::floor((starts[axis] - margin) / pitch)));
            const auto last = std::min<std::uint64_t>(
                cells, std::uint64_t(std::max(0., std::ceil((ends[axis] - margin) / pitch))) + 1);
            if (last > first + 512)
                continue;
            for (auto i = first; i < last; ++i)
                for (auto edge : {std::uint64_t(0), std::uint64_t(extent)}) {
                    const auto position = (margin + i * pitch + edge - starts[axis]) * scales[axis];
                    if (axis)
                        draw->AddLine({origin.x, origin.y + float(position)},
                                      {origin.x + float(view.width), origin.y + float(position)},
                                      IM_COL32(220, 225, 235, 115));
                    else
                        draw->AddLine({origin.x + float(position), origin.y},
                                      {origin.x + float(position), origin.y + float(view.height)},
                                      IM_COL32(220, 225, 235, 115));
                }
        }
        const auto selected = sprite_frame_region(view.texture_width, view.texture_height, grid,
                                                  pending_, crop, regions);
        draw->AddRect(
            {origin.x + float((selected.x - starts[0]) * scales[0]),
             origin.y + float((selected.y - starts[1]) * scales[1])},
            {origin.x + float((std::uint64_t(selected.x) + selected.width - starts[0]) * scales[0]),
             origin.y +
                 float((std::uint64_t(selected.y) + selected.height - starts[1]) * scales[1])},
            IM_COL32(255, 190, 65, 255), 0, 0, 2 * interface_scale);
    }
    bool contents(const SpriteFrameContext& context, std::uint32_t& value) {
        SpriteSheetImage source;
        try {
            if (image_source)
                source = image_source(context.material);
        } catch (const std::exception& error) {
            source.status = error.what();
        }
        ImGui::TextUnformatted("Choose Sprite frame");
        ImGui::TextWrapped("Wheel: zoom | Right-drag: pan | Arrows: frame | Enter: choose");
        if (button("Fit image", "Show the whole sheet."))
            view_ = {};
        FORGE_UI_PROBE("sprite:fit-sheet");
        bool valid = source.ready(), choose = false;
        SpritePixelRegion first;
        std::uint64_t last = 0;
        if (valid) {
            if (image_revision_ && image_revision_ != source.revision) {
#ifdef FORGE_UI_FIXTURE
                close_reason_ = "image revision changed";
#endif
                message_ = "The image changed. Open Choose frame again.";
                ImGui::CloseCurrentPopup();
                return false;
            }
            image_revision_ = source.revision;
            try {
                first = sprite_frame_region(source.width, source.height, context.grid, 0,
                                            context.crop, context.frame_regions);
                last = std::min<std::uint64_t>(
                    UINT32_MAX, sprite_frame_count(context.grid, context.frame_regions) - 1);
                pending_ = std::uint32_t(std::min<std::uint64_t>(pending_, last));
            } catch (const std::exception& error) {
                valid = false;
                source.status = error.what();
            }
        }
        if (source.width && source.height) {
            if (valid && !context.frame_regions.empty()) {
                const auto selected =
                    sprite_frame_region(source.width, source.height, context.grid, pending_,
                                        context.crop, context.frame_regions);
                ImGui::TextDisabled("Image: %u x %u pixels | Frame %u: %u x %u pixels",
                                    source.width, source.height, pending_, selected.width,
                                    selected.height);
            } else if (valid)
                ImGui::TextDisabled("Image: %u x %u pixels | Cell: %u x %u pixels", source.width,
                                    source.height, first.width, first.height);
            else
                ImGui::TextDisabled("Image: %u x %u pixels", source.width, source.height);
        }
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(32.f, available.x),
                          std::max(32.f, available.y - 2 * ImGui::GetFrameHeightWithSpacing())};
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##sheet", size,
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_EnableNav);
        FORGE_UI_PROBE("sprite:sheet-canvas");
        ImGui::SetItemDefaultFocus();
#ifdef FORGE_UI_FIXTURE
        canvas_focused_ = ImGui::IsItemFocused();
#endif
        ImGui::GetWindowDrawList()->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                                                  ImGui::GetColorU32(ImGuiCol_FrameBg));
        if (valid) {
            const auto& io = ImGui::GetIO();
            auto view = sprite_sheet_viewport(source.width, source.height, size.x, size.y, view_);
            const double x = io.MousePos.x - origin.x, y = io.MousePos.y - origin.y;
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
                sprite_sheet_pan(view_, source.width, source.height, size.x, size.y,
                                 ImGui::IsItemActivated() ? 0 : io.MouseDelta.x,
                                 ImGui::IsItemActivated() ? 0 : io.MouseDelta.y);
            if (ImGui::IsItemHovered() && !io.KeyCtrl &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
                sprite_sheet_zoom_at(view_, source.width, source.height, size.x, size.y, x, y,
                                     io.MouseWheel);
            }
            const auto old_pending = pending_;
            if (ImGui::IsItemFocused() && !io.WantTextInput &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                for (auto key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
                                 ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Enter})
                    ImGui::SetKeyOwner(key, ImGui::GetItemID());
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && pending_)
                    --pending_;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && pending_ < last)
                    ++pending_;
                const auto stride = context.frame_regions.empty() ? context.grid.columns : 1;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && pending_ >= stride)
                    pending_ -= stride;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) &&
                    std::uint64_t(pending_) + stride <= last)
                    pending_ += stride;
                if (ImGui::IsKeyPressed(ImGuiKey_Home))
                    pending_ = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_End))
                    pending_ = std::uint32_t(last);
                choose |= ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            }
            if (pending_ != old_pending) {
                view = sprite_sheet_viewport(source.width, source.height, size.x, size.y, view_);
                const auto cell =
                    sprite_frame_region(source.width, source.height, context.grid, pending_,
                                        context.crop, context.frame_regions);
                const auto reveal = [](double center, double minimum, double maximum, double span) {
                    return maximum - minimum >= span
                               ? (minimum + maximum) / 2
                               : std::clamp(center, maximum - span / 2, minimum + span / 2);
                };
                view_.center_x =
                    reveal((view.region[0] + view.region[2]) / 2, double(cell.x) / source.width,
                           double(std::uint64_t(cell.x) + cell.width) / source.width,
                           view.region[2] - view.region[0]);
                view_.center_y =
                    reveal((view.region[1] + view.region[3]) / 2, double(cell.y) / source.height,
                           double(std::uint64_t(cell.y) + cell.height) / source.height,
                           view.region[3] - view.region[1]);
                sprite_sheet_clamp_center(view_, sprite_sheet_viewport(source.width, source.height,
                                                                       size.x, size.y, view_));
            }
            view = sprite_sheet_viewport(source.width, source.height, size.x, size.y, view_);
            std::array<float, 4> region;
            std::transform(view.region.begin(), view.region.end(), region.begin(),
                           [](double v) { return float(v); });
            const double reduction = std::min(1., 2048 / std::max(view.width, view.height));
            ImTextureID image{};
            try {
                image = source.render(std::max(1u, unsigned(std::round(view.width * reduction))),
                                      std::max(1u, unsigned(std::round(view.height * reduction))),
                                      region);
            } catch (const std::exception& error) {
                source.status = error.what();
            }
            valid = image != ImTextureID{};
            if (valid) {
                auto* draw = ImGui::GetWindowDrawList();
                const ImVec2 minimum{origin.x + float(view.left), origin.y + float(view.top)};
                const ImVec2 maximum{minimum.x + float(view.width), minimum.y + float(view.height)};
                draw->AddImage(ImTextureRef{image}, minimum, maximum);
                draw->PushClipRect(minimum, maximum, true);
                if (context.crop) {
                    const auto area =
                        sprite_grid_region(source.width, source.height, {}, 0, context.crop);
                    const auto point = [&](double x, double y) {
                        return ImVec2{
                            minimum.x + float((x / source.width - view.region[0]) /
                                              (view.region[2] - view.region[0]) * view.width),
                            minimum.y + float((y / source.height - view.region[1]) /
                                              (view.region[3] - view.region[1]) * view.height)};
                    };
                    draw->PushClipRect(point(area.x, area.y),
                                       point(std::uint64_t(area.x) + area.width,
                                             std::uint64_t(area.y) + area.height),
                                       true);
                }
                frame_lines(view, context.grid, first, minimum, context.crop,
                            context.frame_regions);
                if (context.crop)
                    draw->PopClipRect();
                draw->PopClipRect();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    if (const auto pixel = view.pixel_at(x, y))
                        if (const auto frame = sprite_sheet_frame_at(
                                source.width, source.height, context.grid, (*pixel)[0], (*pixel)[1],
                                context.crop, context.frame_regions)) {
                            pending_ = *frame;
                            choose = true;
                        }
            }
        }
        if (!valid) {
            auto* draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            draw->AddText(
                ImGui::GetFont(), ImGui::GetFontSize(), origin, ImGui::GetColorU32(ImGuiCol_Text),
                source.status.empty() ? "Preparing image preview..." : source.status.c_str(),
                nullptr, size.x);
            draw->PopClipRect();
        }
        ImGui::Text(
            "Frame %u | Row %llu | Column %llu", pending_,
            static_cast<unsigned long long>(pending_ / std::max(1u, context.grid.columns)) + 1,
            static_cast<unsigned long long>(pending_ % std::max(1u, context.grid.columns)) + 1);
        ImGui::BeginDisabled(!valid);
        choose |= button("Use frame", "Choose the highlighted frame as one ordinary edit.");
        FORGE_UI_PROBE("sprite:use-frame");
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool close = button("Cancel", "Close without choosing a frame.");
        FORGE_UI_PROBE("sprite:cancel-frame");
        if (!valid && source.retry) {
            ImGui::SameLine();
            if (button("Retry image", "Try loading the selected image again."))
                source.retry();
            FORGE_UI_PROBE("sprite:retry-sheet");
        }
        choose &= valid;
        if (choose || close) {
#ifdef FORGE_UI_FIXTURE
            close_reason_ = choose ? "confirmed" : "cancel button";
#endif
            ImGui::CloseCurrentPopup();
        }
        if (choose) {
            const bool changed = value != pending_;
            value = pending_;
            return changed;
        }
        return false;
    }
    ImGuiID popup_ = 0;
#ifdef FORGE_UI_FIXTURE
    std::string close_reason_;
    bool canvas_focused_ = false;
    unsigned draw_calls_ = 0;
#endif
    bool submitted_ = false;
    std::string owner_, message_;
    AssetRef<MaterialAsset> material_;
    std::array<std::uint32_t, 6> grid_{};
    std::optional<SpritePixelRegion> crop_;
    std::vector<SpriteFrameRegion> frame_regions_;
    std::string paste_owner_, paste_error_;
    SpriteSheetView view_;
    std::uint32_t pending_ = 0;
    std::optional<ResourceIdentity> image_revision_;
};
} // namespace forge::ui
