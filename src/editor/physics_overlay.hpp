#pragma once
#include "../collision_selection.hpp"
#include "../physics_debug.hpp"
#include "spatial_helpers.hpp"
#include <future>
namespace forge::ui {
// Selected-object, Scene-only inspection. Preparation uses immutable copied input;
// no editor Flecs world or live simulation body is touched by the worker.
class PhysicsOverlay {
  public:
    bool visible = false;
    const std::string& status() const { return status_; }
    bool ready() const {
        return visible && !geometry_.triangles.empty() && good_ == key_ && error_.empty();
    }
    ~PhysicsOverlay() {
        if (job_.valid())
            job_.wait();
    }
    void draw(const Json& scene, const std::string& selected, const EditorCamera& camera,
              ImVec2 origin, ImVec2 area, const std::filesystem::path& project,
              std::shared_ptr<const AssetCatalog> catalog, const Json& runtime = {}) {
        if (!visible || selected.empty() || !catalog)
            return;
        const Json* row = nullptr;
        for (const auto& e : scene.at("entities"))
            if (e.at("id") == selected) {
                row = &e;
                break;
            }
        if (!row)
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
            if (name == "forge.physics_body" || name == "forge.character_controller" ||
                name.ends_with("_collider"))
                components[name] = value;
        if (!components.contains("forge.physics_body") &&
            !components.contains("forge.character_controller"))
            return;
        LocalTransform pose;
        bool disabled = false, stale = false, pose_valid = false;
        std::string note;
        const Json* character = nullptr;
        const bool show_character = physics_debug_uses_character(components);
        if (show_character && runtime.is_object() && runtime.contains("character_debug") &&
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
            pose = decompose(world, &hint);
            pose_valid = true;
            const bool crouched = character && character->value("crouched", false);
            ResourceLease<CollisionAsset> collision;
            std::string revision;
            if (!show_character && components.contains("forge.asset_collider")) {
                const auto id = components["forge.asset_collider"]["asset"].get<AssetId>();
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
                           : !components["forge.physics_body"].value("enabled", true);
            key_ = components.dump() +
                   Json::array({pose.scale.x, pose.scale.y, pose.scale.z}).dump() + revision +
                   (crouched ? "crouched" : "standing");
            if (!job_.valid() && key_ != attempt_) {
                const auto snapshot = snapshot_physics_debug_collision(collision);
                work_ = attempt_ = key_;
                work_entity_ = entity_;
                work_project_ = project_;
                job_ = std::async(
                    std::launch::async, [components, scale = pose.scale, snapshot, crouched] {
                        return prepare_physics_debug(components, scale, snapshot, crouched);
                    });
            }
            if (good_ != key_) {
                stale = true;
                if (note.empty())
                    note = error_.empty() ? "Preparing collision preview" : error_;
            }
            if (note.empty())
                note = disabled ? "Collision disabled — authored shape preview"
                                : "Collision geometry — selected object";
        } catch (const std::exception& e) {
            error_ = e.what();
            note = error_;
            stale = true;
        }
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + area.x, origin.y + area.y}, true);
        const auto color = !error_.empty() ? IM_COL32(255, 112, 120, 230)
                           : stale         ? IM_COL32(246, 192, 95, 225)
                           : disabled      ? IM_COL32(160, 170, 185, 210)
                                           : IM_COL32(74, 231, 187, 230);
        // Error/stale geometry is explicitly marked; it never claims realization.
        pose.scale = {1, 1, 1};
        const auto world = affine_transform(pose);
        if (pose_valid)
            for (const auto& triangle : geometry_.triangles)
                for (unsigned i = 0; i < 3; ++i) {
                    const auto& a = triangle[i];
                    const auto& b = triangle[(i + 1) % 3];
                    SpatialHelpers::draw_line(draw, camera, origin, area,
                                              world.point({a[0], a[1], a[2]}),
                                              world.point({b[0], b[1], b[2]}), color);
                }
        if (geometry_.truncated)
            note += " | preview triangle limit reached";
        if (character && character->contains("ground")) {
            static constexpr const char* grounds[] = {"On ground", "Steep ground", "Unsupported",
                                                      "In air"};
            note +=
                " | " + std::string(grounds[std::min(3u, character->at("ground").get<unsigned>())]);
            const auto p = character->at("ground_position").get<Double3>(),
                       normal = character->at("ground_normal").get<Double3>();
            SpatialHelpers::draw_line(draw, camera, origin, area, p, helper_add(p, normal),
                                      IM_COL32(120, 190, 255, 255));
            const auto velocity = character->at("velocity").get<Double3>();
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
