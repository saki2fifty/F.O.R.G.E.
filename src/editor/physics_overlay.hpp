#pragma once
#include "../collision_selection.hpp"
#include "../physics_debug.hpp"
#include "physics_joint_guide.hpp"
#include "spatial_helpers.hpp"
#include "tile_map_inspector.hpp"
#include <cstdio>
#include <future>
#include <numbers>
namespace forge::ui {
// Selected-object, Scene-only inspection. Preparation uses immutable copied input;
// no editor Flecs world or live simulation body is touched by the worker.
class PhysicsOverlay {
  public:
    bool visible = false;
    bool joint_visible = true;
    const std::string& status() const { return status_; }
    bool ready() const {
        return visible && body_selected_ && !joint_selected_ && !good_.empty() && good_ == key_ &&
               error_.empty() && !stale_;
    }
    ~PhysicsOverlay() {
        if (job_.valid())
            job_.wait();
    }
    void draw(const Json& scene, const std::string& selected, const EditorCamera& camera,
              ImVec2 origin, ImVec2 area, const std::filesystem::path& project,
              std::shared_ptr<const AssetCatalog> catalog, const Json& runtime = {},
              const PublishedTileMapReader& read_tile_map = {}) {
        joint_selected_ = false;
        body_selected_ = false;
        if (selected.empty())
            return;
        const Json* row = nullptr;
        for (const auto& e : scene.at("entities"))
            if (e.at("id") == selected) {
                row = &e;
                break;
            }
        if (!row)
            return;
        const bool spatial_joint = row->at("components").contains("forge.physics_joint");
        const bool planar_joint = row->at("components").contains("forge.physics_joint_2d");
        joint_selected_ = spatial_joint || planar_joint;
        if (joint_selected_) {
            status_.clear();
            if (joint_visible) {
                float offset = 0;
                std::string spatial_note;
                if (spatial_joint)
                    if (const auto guide = prepare_physics_joint_guide(scene, selected)) {
                        offset = draw_joint(*guide, camera, origin, area);
                        spatial_note = status_;
                    }
                if (planar_joint)
                    if (const auto guide = prepare_physics_joint_2d_guide(scene, selected)) {
                        draw_joint(*guide, camera, origin, area, offset);
                        if (!spatial_note.empty())
                            status_ = spatial_note + "\n" + status_;
                    }
            }
            return;
        }
        if (!visible || !catalog)
            return;
        if (entity_ != selected || project_ != project) {
            if (pool_ && asset_)
                pool_->unload({asset_});
            ticket_ = {};
            asset_ = {};
            geometry_ = {};
            key_.clear();
            attempt_.clear();
            good_.clear();
            error_.clear();
            status_.clear();
            entity_ = selected;
            project_ = project;
        }
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = job_.get();
                if (work_entity_ == entity_ && work_project_ == project_) {
                    geometry_ = std::move(result);
                    good_ = work_;
                    error_.clear();
                }
            } catch (const std::exception& e) {
                if (work_entity_ == entity_ && work_project_ == project_)
                    error_ = e.what();
            }
        }
        Json components = Json::object();
        for (const auto& [name, value] : row->at("components").items())
            if (name == "forge.physics_body" || name == "forge.physics_body_2d" ||
                name == "forge.character_controller" || name == "forge.character_controller_2d" ||
                name.ends_with("_collider") || name.ends_with("_collider_2d"))
                components[name] = value;
        if (components.contains("forge.tile_map_collider_2d") &&
            row->at("components").contains("forge.tile_map_renderer"))
            components["forge.tile_map_renderer"] =
                row->at("components")["forge.tile_map_renderer"];
        if (!components.contains("forge.physics_body") &&
            !components.contains("forge.physics_body_2d") &&
            !components.contains("forge.character_controller") &&
            !components.contains("forge.character_controller_2d"))
            return;
        body_selected_ = true;
        LocalTransform pose;
        bool disabled = false, stale = false, pose_valid = false;
        std::string note;
        const Json* character = nullptr;
        const bool show_character = physics_debug_uses_character(components);
        const bool show_character_2d = physics_debug_uses_character_2d(components);
        const bool show_planar = physics_debug_uses_planar(components);
        if ((show_character || show_character_2d) && runtime.is_object() &&
            runtime.contains("character_debug") &&
            runtime["character_debug"].value("entity", std::string{}) == selected)
            character = &runtime["character_debug"];
        try {
            if (!row->value("spatial_resolved", false))
                throw std::runtime_error("Spatial transform is unresolved");
            AffineTransform world;
            for (unsigned i = 0; i < 12; ++i)
                world.m[i] = row->at("world_affine").at(i).get<double>();
            const auto& c = row->at("components");
            LocalTransform hint;
            if (c.contains("forge.local_scale")) {
                const auto& v = c["forge.local_scale"];
                hint.scale = {v.at("x"), v.at("y"), v.at("z")};
            }
            if (c.contains("forge.local_rotation")) {
                const auto& v = c["forge.local_rotation"];
                hint.rotation = {v.at("x"), v.at("y"), v.at("z"), v.at("w")};
            }
            pose = physics_debug_pose(components, world, hint);
            pose_valid = true;
            const bool crouched = character && character->value("crouched", false);
            ResourceLease<CollisionAsset> collision;
            std::shared_ptr<const TileMapData> tile_map;
            std::string revision;
            const bool show_tile_map = show_planar && !show_character_2d &&
                                       components.contains("forge.tile_map_collider_2d");
            if (show_tile_map) {
                const auto& reference = components.at("forge.tile_map_renderer").at("map");
                if (reference.is_null() || !read_tile_map)
                    throw std::runtime_error("Assign a published Tile Map to preview collision.");
                const auto view = read_tile_map({reference.get<AssetId>()});
                tile_map = view.cells;
                revision = view.revision;
                stale = !view.current;
                if (stale)
                    note = view.status;
                if (!tile_map)
                    throw std::runtime_error(note.empty() ? "Preparing published Tile Map" : note);
            }
            if (!show_character && !show_planar && components.contains("forge.asset_collider")) {
                const auto& reference = components["forge.asset_collider"]["asset"];
                if (reference.is_null())
                    throw std::runtime_error("Choose a Collision asset in Asset Collider to "
                                             "preview this body's collision shape.");
                const auto id = reference.get<AssetId>();
                if (!pool_)
                    pool_ = std::make_unique<ResourcePool<CollisionAsset>>(
                        ResourcePoolLimits{1, 8, 8, 128ull * 1024 * 1024});
                pool_->pump();
                const auto found = catalog->records().find(id);
                if (found == catalog->records().end())
                    throw std::runtime_error("Collision asset is missing");
                const auto selection = found->second.metadata.at("forge.import").dump();
                if (asset_ != id || selection_ != selection) {
                    if (asset_ && asset_ != id)
                        pool_->unload({asset_});
                    ticket_ = request_collision(*pool_, project, catalog, {id});
                    asset_ = id;
                    selection_ = selection;
                }
                pool_->pump();
                collision = pool_->current({id});
                const auto info = ticket_.inspect();
                stale = info.state != ResourceState::Ready;
                if (stale)
                    note =
                        info.diagnostic.empty() ? "Preparing collision revision" : info.diagnostic;
                if (!collision)
                    throw std::runtime_error(note.empty() ? "Collision is not prepared" : note);
                revision = collision.identity().revision;
                if (runtime.is_object())
                    for (const auto& resource : runtime.value("collision_resources", Json::array()))
                        if (resource.at("asset") == id.str() &&
                            resource.value("revision", std::string{}) != revision) {
                            stale = true;
                            note = "Published collision differs from the runtime revision; preview "
                                   "is not the active collider";
                        }
            }
            disabled = show_character
                           ? !components["forge.character_controller"].value("enabled", true)
                           : !components[show_character_2d ? "forge.character_controller_2d"
                                         : show_planar     ? "forge.physics_body_2d"
                                                           : "forge.physics_body"]
                                  .value("enabled", true);
            key_ = components.dump() +
                   Json::array({pose.scale.x, pose.scale.y, pose.scale.z}).dump() + revision +
                   (crouched ? "crouched" : "standing");
            if (!job_.valid() && key_ != attempt_) {
                const auto snapshot = snapshot_physics_debug_collision(collision);
                work_ = attempt_ = key_;
                work_entity_ = entity_;
                work_project_ = project_;
                job_ = std::async(std::launch::async,
                                  [components, scale = pose.scale, snapshot, crouched, tile_map] {
                                      return prepare_physics_debug(components, scale, snapshot,
                                                                   crouched, 8192, tile_map.get());
                                  });
            }
            if (good_ != key_) {
                stale = true;
                if (note.empty())
                    note = error_.empty() ? "Preparing collision preview" : error_;
            }
            if (note.empty()) {
                note = disabled ? "Collision disabled — authored shape preview"
                                : "Collision geometry — selected object";
                if (show_character_2d)
                    note = disabled ? "2D character disabled - authored feet outline"
                                    : "2D character - feet outline";
                else if (show_planar)
                    note = disabled ? "2D collision disabled - authored outline"
                                    : (components["forge.physics_body_2d"].value("sensor", false)
                                           ? "2D sensor - authored outline"
                                           : "2D collision - authored outline");
                if (show_tile_map)
                    note = disabled ? "Tile Map collision disabled - published layer outline"
                                    : "Tile Map collision - published layer outline";
                if (show_tile_map && good_ == key_ && geometry_.lines.empty())
                    note += " | selected layer is empty";
                if (show_tile_map && runtime.is_object() &&
                    runtime.value("native_2d_realized", false))
                    note += " | runtime cell edits are not shown here";
            }
        } catch (const std::exception& e) {
            error_ = e.what();
            note = error_;
            stale = true;
        }
        stale_ = stale;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + area.x, origin.y + area.y}, true);
        const auto color = !error_.empty() ? IM_COL32(255, 112, 120, 230)
                           : stale         ? IM_COL32(246, 192, 95, 225)
                           : disabled      ? IM_COL32(160, 170, 185, 210)
                                           : IM_COL32(74, 231, 187, 230);
        // Error/stale geometry is explicitly marked; it never claims realization.
        pose.scale = {1, 1, 1};
        const auto world = affine_transform(pose);
        if (pose_valid) {
            for (const auto& triangle : geometry_.triangles)
                for (unsigned i = 0; i < 3; ++i) {
                    const auto& a = triangle[i];
                    const auto& b = triangle[(i + 1) % 3];
                    SpatialHelpers::draw_line(draw, camera, origin, area,
                                              world.point({a[0], a[1], a[2]}),
                                              world.point({b[0], b[1], b[2]}), color);
                }
            for (const auto& line : geometry_.lines)
                SpatialHelpers::draw_line(draw, camera, origin, area,
                                          world.point({line[0][0], line[0][1], line[0][2]}),
                                          world.point({line[1][0], line[1][1], line[1][2]}), color);
        }
        if (geometry_.truncated)
            note +=
                show_planar ? " | preview line limit reached" : " | preview triangle limit reached";
        if (character && character->contains("ground")) {
            static constexpr const char* grounds[] = {"On ground", "Steep ground", "Unsupported",
                                                      "In air"};
            note +=
                " | " + std::string(grounds[std::min(3u, character->at("ground").get<unsigned>())]);
            const auto vector = [&](const char* field, bool point = false) -> Double3 {
                if (show_character_2d) {
                    const auto value = character->at(field).get<Double2>();
                    return {value[0], value[1], point ? pose.translation.z : 0};
                }
                return character->at(field).get<Double3>();
            };
            if (character->contains("support")) {
                const auto p = vector("ground_position", true), normal = vector("ground_normal");
                SpatialHelpers::draw_line(draw, camera, origin, area, p, helper_add(p, normal),
                                          IM_COL32(120, 190, 255, 255));
            }
            const auto velocity = vector("velocity");
            const Double3 feet{pose.translation.x, pose.translation.y, pose.translation.z};
            SpatialHelpers::draw_line(draw, camera, origin, area, feet,
                                      helper_add(feet, velocity, .25),
                                      IM_COL32(255, 220, 110, 255));
            if (character->value("shape_blocked", false))
                note += " | shape/placement blocked";
            if (character->contains("support")) {
                const auto support = character->at("support").get<EntityRef>();
                for (const auto& candidate : scene.at("entities"))
                    if (candidate.at("id") == support.entity.str()) {
                        note += " | support: " + candidate.value("name", std::string("Entity"));
                        break;
                    }
            }
        }
        const auto width = std::max(40.f, area.x - 28.f);
        const auto extent = ImGui::CalcTextSize(note.c_str(), nullptr, false, width);
        const auto text =
            ImVec2{origin.x + 10, std::max(origin.y + 10, origin.y + area.y - extent.y -
                                                              ImGui::GetTextLineHeight() - 12)};
        draw->AddRectFilled({text.x - 4, text.y - 3},
                            {origin.x + area.x - 10, text.y + extent.y + 3},
                            IM_COL32(14, 20, 27, 225), 3);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text, color, note.c_str(), nullptr,
                      width);
        draw->PopClipRect();
        status_ = note;
    }

  private:
    bool joint_selected_ = false, body_selected_ = false, stale_ = true;
    float draw_joint(const PhysicsJointGuide& guide, const EditorCamera& camera, ImVec2 origin,
                     ImVec2 area, float note_offset = 0) {
        static constexpr const char* kinds[] = {"Distance", "Hinge",  "Point",
                                                "Fixed",    "Slider", "Swing Twist"};
        const auto color = !guide.error.empty() ? IM_COL32(255, 112, 120, 230)
                           : !guide.enabled     ? IM_COL32(160, 170, 185, 230)
                           : guide.valid        ? IM_COL32(74, 231, 187, 230)
                                                : IM_COL32(255, 112, 120, 230);
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + area.x, origin.y + area.y}, true);
        const std::string prefix = guide.planar ? "Authored 2D " : "Authored ";
        std::string note =
            guide.kind < 6 ? prefix + kinds[guide.kind] + " joint" : prefix + "joint";
        if (guide.planar)
            note += " | XY, depth ignored";
        if (!guide.enabled)
            note += " | disabled";
        if (!guide.error.empty())
            note += " | " + guide.error;
        for (unsigned i = 0; i < 2; ++i) {
            const auto& body = guide.bodies[i];
            note += std::string(" | ") + (i ? "B: " : "A: ") +
                    (body.name.empty() ? "unavailable" : body.name);
            if (!body.error.empty())
                note += " (" + body.error + ")";
            if (!body.available)
                continue;
            const auto marker = i ? IM_COL32(255, 194, 88, 240) : IM_COL32(105, 185, 255, 240);
            for (const auto& axis : body.axes)
                SpatialHelpers::draw_line(draw, camera, origin, area,
                                          helper_add(body.anchor, axis, -.08),
                                          helper_add(body.anchor, axis, .08), marker);
            if (guide.kind == 1 || guide.kind == 3 || guide.kind == 4 || guide.kind == 5) {
                SpatialHelpers::draw_line(draw, camera, origin, area, body.anchor,
                                          helper_add(body.anchor, body.axes[guide.axis[i]], .6),
                                          marker);
                SpatialHelpers::draw_line(draw, camera, origin, area, body.anchor,
                                          helper_add(body.anchor, body.axes[guide.normal[i]], .35),
                                          marker);
                if (guide.kind == 3) {
                    const auto& z = body.axes[guide.axis[i]];
                    const auto& x = body.axes[guide.normal[i]];
                    const Double3 y{z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2],
                                    z[0] * x[1] - z[1] * x[0]};
                    SpatialHelpers::draw_line(draw, camera, origin, area, body.anchor,
                                              helper_add(body.anchor, y, .35), marker);
                }
            }
            if (guide.kind == 5 && i == 0) {
                const auto& twist = body.axes[guide.axis[0]];
                const auto& plane = body.axes[guide.normal[0]];
                const Double3 normal{plane[1] * twist[2] - plane[2] * twist[1],
                                     plane[2] * twist[0] - plane[0] * twist[2],
                                     plane[0] * twist[1] - plane[1] * twist[0]};
                // Native cone limits form an ellipse in swing-quaternion space.
                // Project its boundary through the rotated twist direction. Length
                // is display-only; this guide does not inspect or modify the solver.
                const auto effective = [](float angle) {
                    return angle < .5f ? 0.f : angle > 179.5f ? 180.f : angle;
                };
                const auto sy =
                    std::sin(effective(guide.swing_plane_angle) * std::numbers::pi / 360);
                const auto sz =
                    std::sin(effective(guide.swing_normal_angle) * std::numbers::pi / 360);
                Double3 previous{};
                for (unsigned step = 0; step <= 48; ++step) {
                    const auto angle = 2 * std::numbers::pi * step / 48;
                    const auto y = sy * std::cos(angle), z = sz * std::sin(angle);
                    const auto w = std::sqrt(std::max(0., 1 - y * y - z * z));
                    auto direction =
                        helper_add(helper_add(helper_add({}, twist, 1 - 2 * (y * y + z * z)),
                                              normal, 2 * w * z),
                                   plane, -2 * w * y);
                    const auto point = helper_add(body.anchor, direction, .6);
                    if (step)
                        SpatialHelpers::draw_line(draw, camera, origin, area, previous, point,
                                                  marker);
                    if (step % 12 == 0 && step < 48)
                        SpatialHelpers::draw_line(draw, camera, origin, area, body.anchor, point,
                                                  marker);
                    previous = point;
                }
                for (const auto degrees : {guide.min_angle, guide.max_angle}) {
                    const auto angle = degrees * std::numbers::pi / 180;
                    const auto direction =
                        helper_add(helper_add({}, normal, std::cos(angle)), plane, std::sin(angle));
                    SpatialHelpers::draw_line(draw, camera, origin, area, body.anchor,
                                              helper_add(body.anchor, direction, .35),
                                              IM_COL32(242, 142, 240, 240));
                }
            }
            if (guide.planar && guide.kind == 1 && i == 0 && guide.limits_enabled) {
                const auto& x = body.axes[0];
                const auto& y = body.axes[1];
                Double3 previous{};
                for (unsigned step = 0; step <= 32; ++step) {
                    const auto degrees =
                        guide.min_angle + (guide.max_angle - guide.min_angle) * (double(step) / 32);
                    const auto angle = degrees * std::numbers::pi / 180;
                    const auto point = helper_add(helper_add(body.anchor, x, .6 * std::cos(angle)),
                                                  y, .6 * std::sin(angle));
                    if (step)
                        SpatialHelpers::draw_line(draw, camera, origin, area, previous, point,
                                                  color);
                    if (step == 0 || step == 32)
                        SpatialHelpers::draw_line(draw, camera, origin, area, body.anchor, point,
                                                  IM_COL32(242, 142, 240, 240));
                    previous = point;
                }
            }
            if (guide.kind == 4 && i == 0 && guide.limits_enabled) {
                const auto low =
                    helper_add(body.anchor, body.axes[guide.axis[0]], guide.min_travel);
                const auto high =
                    helper_add(body.anchor, body.axes[guide.axis[0]], guide.max_travel);
                SpatialHelpers::draw_line(draw, camera, origin, area, low, high, color);
                for (const auto& end : {low, high})
                    SpatialHelpers::draw_line(draw, camera, origin, area,
                                              helper_add(end, body.axes[guide.normal[0]], -.15),
                                              helper_add(end, body.axes[guide.normal[0]], .15),
                                              marker);
            }
            if (i == 0 && (guide.kind == 1 || guide.kind == 4) && guide.motor_enabled &&
                guide.motor_mode == 1) {
                const auto& axis = body.axes[guide.axis[0]];
                const auto& normal = body.axes[guide.normal[0]];
                Double3 target;
                if (guide.kind == 4)
                    target = helper_add(body.anchor, axis, guide.motor_target);
                else {
                    const double angle = guide.motor_target * std::numbers::pi / 180;
                    const Double3 tangent{axis[1] * normal[2] - axis[2] * normal[1],
                                          axis[2] * normal[0] - axis[0] * normal[2],
                                          axis[0] * normal[1] - axis[1] * normal[0]};
                    target = helper_add(helper_add(body.anchor, normal, .6 * std::cos(angle)),
                                        tangent, .6 * std::sin(angle));
                    SpatialHelpers::draw_line(draw, camera, origin, area, body.anchor, target,
                                              IM_COL32(242, 142, 240, 240));
                }
                SpatialHelpers::draw_line(
                    draw, camera, origin, area, helper_add(target, normal, -.12),
                    helper_add(target, normal, .12), IM_COL32(242, 142, 240, 240));
                SpatialHelpers::draw_line(
                    draw, camera, origin, area, helper_add(target, axis, -.12),
                    helper_add(target, axis, .12), IM_COL32(242, 142, 240, 240));
            }
            const auto p = project_point(
                camera, Vec3{float(body.anchor[0]), float(body.anchor[1]), float(body.anchor[2])},
                area.x, area.y);
            if (p && std::isfinite((*p)[0]) && std::isfinite((*p)[1]))
                draw->AddText({origin.x + (*p)[0] + 6, origin.y + (*p)[1] + 6}, marker,
                              i ? "B" : "A");
        }
        if (guide.bodies[0].available && guide.bodies[1].available) {
            const auto& a = guide.bodies[0].anchor;
            const auto& b = guide.bodies[1].anchor;
            SpatialHelpers::draw_line(draw, camera, origin, area, a, b, color);
            auto gap = helper_add(b, a, -1);
            if (guide.planar)
                gap[2] = 0;
            char metric[64];
            std::snprintf(metric, sizeof(metric), " | anchor gap %.3f m",
                          std::sqrt(gap[0] * gap[0] + gap[1] * gap[1] + gap[2] * gap[2]));
            note += metric;
            note +=
                guide.collide_connected ? " | connected contacts on" : " | connected contacts off";
            if (guide.kind == 4) {
                const auto& axis = guide.bodies[0].axes[guide.axis[0]];
                if (guide.limits_enabled)
                    std::snprintf(metric, sizeof(metric), " | travel %.3f m [%.3f, %.3f]",
                                  gap[0] * axis[0] + gap[1] * axis[1] + gap[2] * axis[2],
                                  guide.min_travel, guide.max_travel);
                else
                    std::snprintf(metric, sizeof(metric), " | travel %.3f m, limits off",
                                  gap[0] * axis[0] + gap[1] * axis[1] + gap[2] * axis[2]);
                note += metric;
            }
            if (guide.planar && guide.kind == 1) {
                if (guide.limits_enabled)
                    std::snprintf(metric, sizeof(metric), " | limits [%.3f, %.3f] deg",
                                  guide.min_angle, guide.max_angle);
                else
                    std::snprintf(metric, sizeof(metric), " | limits off");
                note += metric;
            }
            if (guide.kind == 0) {
                char limits[160];
                if (guide.planar)
                    std::snprintf(limits, sizeof(limits), " | %s %.3f m",
                                  guide.distance_spring_frequency > 0 ? "spring rest" : "length",
                                  guide.min_distance);
                else
                    std::snprintf(limits, sizeof(limits), " | %s range [%.3f, %.3f] m",
                                  guide.distance_spring_frequency > 0 ? "elastic" : "hard",
                                  guide.min_distance, guide.max_distance);
                note += limits;
                if (guide.distance_spring_frequency > 0) {
                    std::snprintf(limits, sizeof(limits), " | spring %.3f Hz / %.3f ratio",
                                  guide.distance_spring_frequency, guide.distance_spring_damping);
                    note += limits;
                }
            }
            if (guide.kind == 5) {
                char limits[128];
                std::snprintf(limits, sizeof(limits),
                              " | swing normal/plane %.3f / %.3f deg | twist [%.3f, %.3f] deg",
                              guide.swing_normal_angle, guide.swing_plane_angle, guide.min_angle,
                              guide.max_angle);
                note += limits;
            }
            if ((guide.kind == 1 || guide.kind == 4) && guide.motor_enabled) {
                char drive[160];
                if (guide.motor_mode == 0)
                    std::snprintf(drive, sizeof(drive), " | drive %.3f %s, budget %.3f %s",
                                  guide.motor_speed, guide.kind == 1 ? "deg/s" : "m/s",
                                  guide.motor_budget, guide.kind == 1 ? "N*m" : "N");
                else
                    std::snprintf(drive, sizeof(drive),
                                  " | target %.3f %s, spring %.3f Hz / %.3f, budget %.3f %s",
                                  guide.motor_target, guide.kind == 1 ? "deg" : "m",
                                  guide.motor_frequency, guide.motor_damping, guide.motor_budget,
                                  guide.kind == 1 ? "N*m" : "N");
                note += drive;
            }
        }
        const auto width = std::max(40.f, area.x - 28.f);
        const auto extent = ImGui::CalcTextSize(note.c_str(), nullptr, false, width);
        const float requested_y =
            origin.y + area.y - extent.y - ImGui::GetTextLineHeight() - 12 - note_offset;
        const auto text =
            ImVec2{origin.x + 10, note_offset ? requested_y : std::max(origin.y + 10, requested_y)};
        draw->AddRectFilled({text.x - 4, text.y - 3},
                            {origin.x + area.x - 10, text.y + extent.y + 3},
                            IM_COL32(14, 20, 27, 225), 3);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text, color, note.c_str(), nullptr,
                      width);
        draw->PopClipRect();
        status_ = note;
        return extent.y + 10;
    }
    std::string entity_, key_, attempt_, good_, work_, error_, selection_, status_;
    std::string work_entity_;
    std::filesystem::path project_, work_project_;
    AssetId asset_;
    std::unique_ptr<ResourcePool<CollisionAsset>> pool_;
    ResourceTicket ticket_;
    PhysicsDebugGeometry geometry_;
    std::future<PhysicsDebugGeometry> job_;
};
} // namespace forge::ui
