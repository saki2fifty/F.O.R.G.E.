#pragma once
#include "builtins.hpp"
#include "camera_shot_blend.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace forge {
namespace detail {
inline const Builtin& camera_shot_source_codec(std::string_view name) {
    for (const auto& type : builtins())
        if (name == type.name)
            return type;
    throw std::logic_error("Camera Shot source component is unavailable");
}
template <class T> T camera_shot_source_value(const Json& value, std::string_view name) {
    return std::get<T>(camera_shot_source_codec(name).decode(value));
}
} // namespace detail

// Source-only policy, shared by the Timeline and worldless command draft.
// The borrowed row index exists only for this validation; it owns no Scene data.
inline void validate_authored_camera_shot(const Json& snapshot, EntityRef controller,
                                          const std::string& id, const Json& camera, double start,
                                          double end, bool enabled = true, double blend = 0) {
    const auto asset = snapshot.at("asset_id").get<AssetId>();
    std::unordered_map<std::string_view, const Json*> rows;
    rows.reserve(snapshot.at("entities").size());
    for (const auto& row : snapshot.at("entities"))
        rows.emplace(row.at("id").get_ref<const std::string&>(), &row);
    const auto component = [&](const std::string& entity, const char* name) -> const Json* {
        const auto found = rows.find(entity);
        if (found == rows.end())
            return nullptr;
        const auto& components = found->second->at("components");
        const auto value = components.find(name);
        return value == components.end() ? nullptr : &*value;
    };
    const auto* sequence_value = component(controller.entity.str(), "forge.animation_sequence");
    if (controller.scene != asset || !sequence_value)
        throw std::runtime_error("Sequence controller is no longer available");
    const auto sequence = detail::camera_shot_source_value<AnimationSequence>(
        *sequence_value, "forge.animation_sequence");
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || start >= end ||
        end > sequence.duration)
        throw std::runtime_error("Shot requires 0 <= start < end <= sequence duration");
    if (!std::isfinite(blend) || blend < 0 || blend > end - start)
        throw std::runtime_error("Camera Shot blend must fit within its interval");
    if (camera.is_null())
        throw std::runtime_error("Select a camera in this Scene");
    const auto reference = camera.get<EntityRef>();
    const auto* camera_value = component(reference.entity.str(), "forge.camera");
    if (reference.scene != asset || !camera_value)
        throw std::runtime_error("Shot camera must be a Camera in this Scene");
    const auto target = detail::camera_shot_source_value<Camera>(*camera_value, "forge.camera");
    validate_shot_camera(target);
    std::size_t count = 0;
    for (const auto& row : snapshot.at("entities")) {
        const auto& other_id = row.at("id").get_ref<const std::string&>();
        if (other_id == id || !row.at("components").contains("forge.camera_shot"))
            continue;
        const auto other = detail::camera_shot_source_value<CameraShot>(
            row.at("components").at("forge.camera_shot"), "forge.camera_shot");
        if (!other.enabled || !other.sequence.entity)
            continue;
        ++count;
        if (!enabled)
            continue;
        if (other.sequence != controller) {
            const auto* owner_value =
                component(other.sequence.entity.str(), "forge.animation_sequence");
            if (other.sequence.scene == asset && owner_value &&
                detail::camera_shot_source_value<AnimationSequence>(*owner_value,
                                                                    "forge.animation_sequence")
                    .enabled)
                throw std::runtime_error("Only one camera-shot sequence may direct this Scene");
        } else {
            if (start < other.end_seconds && end > other.start_seconds)
                throw std::runtime_error(
                    "Shot overlaps another camera shot; use adjacent intervals");
            if (blend > 0 && other.end_seconds == start) {
                const auto* previous = component(other.camera.entity.str(), "forge.camera");
                if (!previous)
                    throw std::runtime_error("Previous shot Camera is unavailable");
                validate_camera_shot_blend(
                    detail::camera_shot_source_value<Camera>(*previous, "forge.camera"), target);
            }
        }
    }
    if (enabled && count >= 256)
        throw std::runtime_error("Camera Shot limit (256) exceeded");
}

inline CameraShot authored_camera_shot(const Json& row) {
    if (!row.at("components").contains("forge.camera_shot"))
        throw std::runtime_error("Choose a Camera Shot entity");
    return detail::camera_shot_source_value<CameraShot>(
        row.at("components").at("forge.camera_shot"), "forge.camera_shot");
}
inline double camera_shot_duplicate_start(const Json& snapshot, const CameraShot& shot,
                                          double earliest, double duration) {
    const double length = shot.end_seconds - shot.start_seconds;
    if (!std::isfinite(earliest) || earliest < 0 || earliest > duration)
        throw std::runtime_error("Copy time must be within the sequence");
    std::vector<CameraShot> spans;
    if (shot.enabled)
        for (const auto& row : snapshot.at("entities")) {
            if (!row.at("components").contains("forge.camera_shot"))
                continue;
            const auto other = authored_camera_shot(row);
            if (other.enabled && other.sequence == shot.sequence)
                spans.push_back(other);
        }
    std::sort(spans.begin(), spans.end(),
              [](const auto& a, const auto& b) { return a.start_seconds < b.start_seconds; });
    double start = earliest;
    for (const auto& span : spans) {
        if (span.end_seconds <= start)
            continue;
        if (start + length <= span.start_seconds)
            break;
        start = span.end_seconds;
    }
    if (start + length > duration)
        throw std::runtime_error(
            "No free interval fits this shot; extend the sequence or choose an earlier time");
    return start;
}
} // namespace forge
