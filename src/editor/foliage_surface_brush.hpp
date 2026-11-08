#pragma once
#include "../mesh_pick.hpp"
#include "scene_cache.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <functional>
#include <imgui.h>
#include <utility>

namespace forge::ui {
// Transient pointer capture only. Projection, object creation and history remain
// with the shared foliage preparation and ordinary Scene command owners.
class FoliageSurfaceBrush {
  public:
    using Pick = std::function<std::optional<MeshSurfaceHit>(double, double)>;
    bool holding() const { return holding_; }
    void cancel_if_missing() {
        if (holding_ && ImGui::GetFrameCount() > input_frame_ + 1)
            cancel();
    }
    void cancel() {
        holding_ = false;
        dabs_ = Json::array();
        markers_.clear();
        hit_.reset();
        view_.reset();
    }
    const std::optional<MeshSurfaceHit>& selection() const { return hit_; }
    std::optional<Json> input(const Scene& scene, EntityId selected, EntityId surface,
                              const Json& config, const EditorCamera& camera, ImVec2 origin,
                              ImVec2 size, bool focused, bool editable, bool hovered,
                              bool activated, bool active, const Pick& pick) {
        input_frame_ = ImGui::GetFrameCount();
        const auto view = viewport_frame_key(0, unsigned(std::max(1.f, size.x)),
                                             unsigned(std::max(1.f, size.y)), camera);
        auto& io = ImGui::GetIO();
        if (holding_ &&
            (!focused || !editable || io.AppFocusLost || scene.asset_id() != scene_ ||
             scene.revision() != revision_ || selected != selected_ || config != config_ ||
             !view_ || *view_ != view || origin.x != origin_.x || origin.y != origin_.y ||
             size.x != size_.x || size.y != size_.y || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
             ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
             ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
            cancel();
            throw std::runtime_error("Foliage stroke cancelled because its view, selection or "
                                     "authoring context changed. No objects were created.");
        }
        if (!editable || !focused || !pick)
            return {};
        if (hovered && activated && active && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            cancel();
            if (!surface || selected != surface)
                throw std::runtime_error("Select the chosen projection surface in Hierarchy "
                                         "before painting foliage.");
            holding_ = true;
            scene_ = scene.asset_id();
            revision_ = scene.revision();
            selected_ = selected;
            config_ = config;
            origin_ = origin;
            size_ = size;
            view_ = view;
        }
        if (!holding_)
            return {};
        try {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (!hovered || !active)
                    return {};
                const auto hit = pick(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
                if (!hit)
                    return {};
                if (hit->entity != surface || hit->lod != 0)
                    throw std::runtime_error("Foliage brush requires the chosen visible LOD0 "
                                             "triangle surface; move closer and try again.");
                if (hit_ && !hit_->same_selection(*hit))
                    throw std::runtime_error(
                        "Published surface selection changed during the stroke.");
                const auto& p = hit->position;
                if (!dabs_.empty()) {
                    const auto& previous = dabs_.back().at("center");
                    if (std::hypot(p[0] - previous.at("x").get<double>(),
                                   p[2] - previous.at("z").get<double>()) <
                        config.at("dab_distance").get<double>())
                        return {};
                }
                const auto count = config.at("dab").at("count").get<unsigned>();
                if (dabs_.size() == 16 || (dabs_.size() + 1) * count > 512)
                    throw std::runtime_error("Stroke exceeds 16 dabs or 512 candidates. Use a "
                                             "shorter stroke or reduce Candidate count.");
                auto dab = config.at("dab");
                dab["center"] = {{"x", p[0]}, {"y", p[1]}, {"z", p[2]}};
                // Different dabs retain deterministic independent seeded layouts.
                dab["seed"] = std::uint32_t(dab.at("seed").get<std::uint32_t>() + dabs_.size());
                dabs_.push_back(std::move(dab));
                markers_.push_back(io.MousePos);
                hit_ = hit;
                return {};
            }
            holding_ = false;
            markers_.clear();
            view_.reset();
            if (dabs_.empty())
                return {};
            return Json{{"entity", config.at("entity")},
                        {"name", config.at("name")},
                        {"dabs", std::exchange(dabs_, Json::array())}};
        } catch (...) {
            cancel();
            throw;
        }
    }
    void overlay(ImVec2 origin, ImVec2 size, bool hovered) const {
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        draw->AddText({origin.x + 8, origin.y + 8}, IM_COL32(140, 240, 160, 255),
                      "FOLIAGE BRUSH | LMB drag: dabs | release: place | Escape: cancel");
        for (const auto marker : markers_)
            draw->AddCircle(marker, 8, IM_COL32(140, 240, 160, 255), 16, 2);
        if (hovered)
            draw->AddCircle(ImGui::GetIO().MousePos, 5, IM_COL32(140, 240, 160, 255), 16, 1.5f);
        draw->PopClipRect();
    }

  private:
    bool holding_ = false;
    AssetId scene_;
    EntityId selected_;
    std::uint64_t revision_ = 0;
    Json config_, dabs_ = Json::array();
    ImVec2 origin_{};
    ImVec2 size_{};
    int input_frame_ = 0;
    std::optional<ViewportFrameKey> view_;
    std::optional<MeshSurfaceHit> hit_;
    std::vector<ImVec2> markers_;
};
} // namespace forge::ui
