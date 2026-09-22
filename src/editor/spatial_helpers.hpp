#pragma once
#include "../builtins.hpp"
#include "authoring.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <forge/render_scene.hpp>
#include <numbers>
namespace forge::ui {
// Editor-only detached visualization. No hidden entities, meshes or authored state.
struct SpatialMarker {
    std::string entity, name, note;
    Double3 position{};
    bool camera = false, enabled = true, selectable = true;
    float offset = 0;
    std::vector<std::pair<Double3, Double3>> lines;
};
inline std::string helper_number(double value) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.4g", value);
    return text;
}
inline Double3 helper_add(Double3 a, Double3 b, double scale = 1) {
    for (unsigned i = 0; i < 3; ++i)
        a[i] += b[i] * scale;
    return a;
}
template <class T> T helper_component(const Json& values, const char* name) {
    detail::validate_components(Json{{name, values.at(name)}});
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == name)
            return std::get<T>(type.decode(values.at(name)));
    throw std::runtime_error("Unknown spatial helper component");
}
inline std::vector<SpatialMarker> spatial_markers(const Json& source, unsigned width,
                                                  unsigned height, double extent) {
    std::vector<SpatialMarker> result;
    const auto policies = extract_node_policies(source);
    for (const auto& row : source.at("entities")) {
        const auto& c = row.at("components");
        for (const bool is_camera : {true, false}) {
            if (!c.contains(is_camera ? "forge.camera" : "forge.light") ||
                row.value("prefab", false) || !row.value("spatial_resolved", false))
                continue;
            const auto policy = policies.entities.at(row.at("id").get<EntityId>());
            if (!policy.valid || !policy.visible)
                continue;
            SpatialMarker marker;
            marker.entity = row.at("id").get<std::string>();
            marker.name = row.value("name", marker.entity);
            marker.camera = is_camera;
            if (c.contains("forge.camera") && c.contains("forge.light"))
                marker.offset = is_camera ? -.7f : .7f;
            marker.selectable = policy.selectable;
            const auto& values = row.at("world_affine");
            AffineTransform world;
            for (unsigned i = 0; i < 12; ++i)
                world.m[i] = values.at(i).get<double>();
            marker.position = world.point({0, 0, 0});
            try {
                auto line = [&](Double3 a, Double3 b) { marker.lines.emplace_back(a, b); };
                auto ring = [&](Double3 center, Double3 right, Double3 up, double radius) {
                    Double3 previous = helper_add(center, right, radius);
                    for (unsigned i = 1; i <= 48; ++i) {
                        const double angle = i * (2 * std::numbers::pi / 48);
                        const auto next =
                            helper_add(helper_add(center, right, radius * std::cos(angle)), up,
                                       radius * std::sin(angle));
                        line(previous, next);
                        previous = next;
                    }
                };
                if (is_camera) {
                    const auto settings = helper_component<Camera>(c, "forge.camera");
                    marker.enabled = settings.enabled;
                    const auto view = camera_view(settings, world, width, height);
                    const bool ortho =
                        settings.projection == unsigned(CameraProjection::Orthographic);
                    const double end =
                        settings.infinite_far ? extent : std::min(extent, settings.far_plane);
                    const bool capped = settings.infinite_far || settings.far_plane > extent;
                    marker.note = settings.infinite_far
                                      ? "Infinite far plane"
                                      : "Far: " + helper_number(settings.far_plane) + " m";
                    if (capped)
                        marker.note += "; guide capped at " + helper_number(extent) + " m";
                    if (end >= settings.near_plane) {
                        std::array<Double3, 4> near{}, far{};
                        for (unsigned i = 0; i < 4; ++i) {
                            auto corner = [&](double depth) {
                                const double x =
                                    (i & 1 ? 1. : -1.) * (ortho ? 1 : depth) / view.projection[0];
                                const double y =
                                    (i & 2 ? 1. : -1.) * (ortho ? 1 : depth) / view.projection[5];
                                return helper_add(
                                    helper_add(helper_add(view.position, view.forward, depth),
                                               view.right, x),
                                    view.up, y);
                            };
                            near[i] = corner(settings.near_plane);
                            far[i] = corner(end);
                            line(near[i], far[i]);
                        }
                        for (unsigned i = 0; i < 4; ++i)
                            for (unsigned bit : {1u, 2u})
                                if (!(i & bit)) {
                                    line(near[i], near[i | bit]);
                                    // An open-ended guide is not an invented far clipping plane.
                                    if (!capped)
                                        line(far[i], far[i | bit]);
                                }
                    }
                } else {
                    const auto settings = helper_component<Light>(c, "forge.light");
                    marker.enabled = settings.enabled;
                    const auto light = light_view(settings, world);
                    const auto kind = LightKind(light.kind);
                    const bool limited = settings.range > 0;
                    const double radius = limited ? std::min(settings.range, extent) : extent;
                    marker.note = kind == LightKind::Directional
                                      ? "Directional light; arrow shows travel direction"
                                  : limited ? "Range: " + helper_number(settings.range) + " m"
                                            : "Unlimited range";
                    if (kind != LightKind::Directional && (!limited || settings.range > extent))
                        marker.note += "; guide capped at " + helper_number(extent) + " m";
                    if (kind == LightKind::Point) {
                        ring(light.position, {1, 0, 0}, {0, 1, 0}, radius);
                        ring(light.position, {1, 0, 0}, {0, 0, 1}, radius);
                        ring(light.position, {0, 1, 0}, {0, 0, 1}, radius);
                    } else {
                        const auto d = light.direction;
                        Double3 right =
                            std::abs(d[1]) < .9 ? Double3{-d[2], 0, d[0]} : Double3{0, d[2], -d[1]};
                        const double length = std::hypot(right[0], right[1], right[2]);
                        for (auto& v : right)
                            v /= length;
                        const Double3 up{d[1] * right[2] - d[2] * right[1],
                                         d[2] * right[0] - d[0] * right[2],
                                         d[0] * right[1] - d[1] * right[0]};
                        const auto tip = helper_add(
                            light.position, d,
                            kind == LightKind::Directional ? std::min(2., extent) : radius);
                        line(light.position, tip);
                        if (kind == LightKind::Directional) {
                            line(tip, helper_add(helper_add(tip, d, -.3), right, .2));
                            line(tip, helper_add(helper_add(tip, d, -.3), right, -.2));
                        } else {
                            // Range is radial distance, not cone height. Valid at a 90-degree half
                            // angle.
                            for (const double angle : {settings.inner_cone, settings.outer_cone}) {
                                const auto center =
                                    helper_add(light.position, d, radius * std::cos(angle));
                                const double r = radius * std::sin(angle);
                                ring(center, right, up, r);
                                for (auto axis : {right, up})
                                    for (double sign : {-1., 1.})
                                        line(light.position, helper_add(center, axis, sign * r));
                            }
                        }
                    }
                }
            } catch (const std::exception& error) {
                marker.lines.clear();
                marker.note = std::string("Invalid camera/light: ") + error.what();
            }
            if (!marker.enabled)
                marker.note += " | Disabled";
            if (!marker.selectable)
                marker.note += " | Selection locked";
            result.push_back(std::move(marker));
        }
    }
    return result;
}
class SpatialHelpers {
  public:
    bool visible = true;
    float size = 24, extent = 10;
    void update(const Json& source, std::uint64_t generation, unsigned width, unsigned height) {
        if (generation_ == generation && width_ == width && height_ == height && extent_ == extent)
            return;
        markers_ = spatial_markers(source, width, height, extent);
        generation_ = generation;
        width_ = width;
        height_ = height;
        extent_ = extent;
    }
    const std::vector<SpatialMarker>& markers() const { return markers_; }
    std::string pick(const EditorCamera& camera, ImVec2 area, ImVec2 mouse) const {
        if (!visible)
            return {};
        std::string hit;
        double nearest = std::numeric_limits<double>::infinity();
        for (const auto& marker : markers_) {
            const auto p = project_point(camera, cast(marker.position), area.x, area.y);
            const double depth =
                dot(subtract(cast(marker.position), camera.eye()), camera.forward());
            if (marker.selectable && p && depth < nearest &&
                std::abs((*p)[0] + marker.offset * size * interface_scale - mouse.x) <=
                    size * interface_scale / 2 &&
                std::abs((*p)[1] - mouse.y) <= size * interface_scale / 2) {
                hit = marker.entity;
                nearest = depth;
            }
        }
        return hit;
    }
    void draw(const EditorCamera& camera, const std::string& selected, ImVec2 origin,
              ImVec2 area) const {
        if (!visible)
            return;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + area.x, origin.y + area.y}, true);
        const SpatialMarker* hovered = nullptr;
        ImRect hover_rect;
        for (const auto& marker : markers_) {
            const auto projected = project_point(camera, cast(marker.position), area.x, area.y);
            const bool active = marker.entity == selected;
            const auto color = active            ? IM_COL32(255, 203, 94, 255)
                               : !marker.enabled ? IM_COL32(141, 150, 163, 210)
                               : marker.camera   ? IM_COL32(124, 195, 238, 240)
                                                 : IM_COL32(241, 220, 139, 240);
            if (active)
                for (const auto& [a, b] : marker.lines)
                    draw_line(draw, camera, origin, area, a, b, color);
            if (!projected || !std::isfinite((*projected)[0]) || !std::isfinite((*projected)[1]) ||
                (*projected)[0] < 0 || (*projected)[1] < 0 || (*projected)[0] > area.x ||
                (*projected)[1] > area.y)
                continue;
            const ImVec2 p{origin.x + (*projected)[0] + marker.offset * size * interface_scale,
                           origin.y + (*projected)[1]};
            const float r = size * interface_scale * .5f;
            draw->AddCircleFilled(p, r, IM_COL32(21, 28, 38, 205), 24);
            auto at = [&](float x, float y) { return ImVec2{p.x + x * r, p.y + y * r}; };
            if (marker.camera) {
                draw->AddRect(at(-.65f, -.4f), at(.2f, .4f), color, 2, 0, 1.5f * interface_scale);
                draw->AddQuad(at(.2f, -.22f), at(.72f, -.55f), at(.72f, .55f), at(.2f, .22f), color,
                              1.5f * interface_scale);
            } else {
                draw->AddCircle(p, r * .35f, color, 16, 1.5f * interface_scale);
                for (unsigned i = 0; i < 8; ++i) {
                    const float angle = i * float(std::numbers::pi / 4), x = std::cos(angle),
                                y = std::sin(angle);
                    draw->AddLine(at(x * .53f, y * .53f), at(x * .8f, y * .8f), color,
                                  interface_scale);
                }
            }
            if (active)
                draw->AddCircle(p, r, color, 24, interface_scale);
            if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(at(-1, -1), at(1, 1))) {
                hovered = &marker;
                hover_rect = {at(-1, -1), at(1, 1)};
            }
#ifdef FORGE_UI_FIXTURE
            if (test::observe_ui)
                test::ui_targets["marker:" + marker.entity] = {at(-1, -1), at(1, 1),
                                                               marker.selectable};
#endif
            if (active)
                draw->AddText({p.x + r + 4, p.y - r}, color, marker.name.c_str());
            if (active && !marker.note.empty())
                draw->AddText({origin.x + 10, origin.y + 15 + ImGui::GetTextLineHeight()}, color,
                              marker.note.c_str());
        }
        draw->PopClipRect();
        const auto id = hovered ? hovered->entity : std::string{};
        if (id != hovered_ || ImGui::IsAnyMouseDown()) {
            hovered_ = id;
            hover_time_ = ImGui::GetTime();
        }
        if (hovered && tooltips && !ImGui::IsAnyMouseDown() && ImGui::GetTime() - hover_time_ >= .5)
            show_help_at((hovered->name + "\n" + hovered->note +
                          "\nClick to select. Scene-only authoring helper.")
                             .c_str(),
                         hover_rect);
    }

  private:
    mutable std::string hovered_;
    mutable double hover_time_ = 0;
    std::vector<SpatialMarker> markers_;
    std::uint64_t generation_ = UINT64_MAX;
    unsigned width_ = 0, height_ = 0;
    float extent_ = 0;
    static Vec3 cast(Double3 value) { return {float(value[0]), float(value[1]), float(value[2])}; }
    static void draw_line(ImDrawList* draw, const EditorCamera& camera, ImVec2 origin, ImVec2 area,
                          Double3 da, Double3 db, ImU32 color) {
        auto a = cast(da), b = cast(db);
        float za = dot(subtract(a, camera.eye()), camera.forward()),
              zb = dot(subtract(b, camera.eye()), camera.forward());
        const float near = EditorCamera::near_plane * 1.01f;
        if (za < near && zb < near)
            return;
        if (za < near || zb < near) {
            const float t = (near - za) / (zb - za);
            Vec3 clipped;
            for (unsigned i = 0; i < 3; ++i)
                clipped[i] = a[i] + (b[i] - a[i]) * t;
            if (za < near)
                a = clipped;
            else
                b = clipped;
        }
        const auto p = project_point(camera, a, area.x, area.y),
                   q = project_point(camera, b, area.x, area.y);
        if (p && q && std::isfinite((*p)[0]) && std::isfinite((*p)[1]) && std::isfinite((*q)[0]) &&
            std::isfinite((*q)[1]))
            draw->AddLine({origin.x + (*p)[0], origin.y + (*p)[1]},
                          {origin.x + (*q)[0], origin.y + (*q)[1]}, color, interface_scale);
    }
};
} // namespace forge::ui
