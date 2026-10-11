#pragma once
#include <forge/transform_animation.hpp>
namespace forge {
using Json = nlohmann::json;
// Render-only substitution. No live ECS or authored/history mutation.
inline bool apply_transform_animation_preview(Json& document, EntityRef target,
                                              const TransformAnimationClip& clip, double time) {
    if (document.at("asset_id") != Json(target.scene))
        return false;
    for (auto& entity : document.at("entities")) {
        if (entity.at("id") != target.entity.str())
            continue;
        auto& c = entity.at("components");
        const auto pose = clip.sample(time);
        if (clip.channels & 1)
            c["forge.local_translation"] = {
                {"x", pose.translation.x}, {"y", pose.translation.y}, {"z", pose.translation.z}};
        if (clip.channels & 2)
            c["forge.local_rotation"] = {{"x", pose.rotation.x},
                                         {"y", pose.rotation.y},
                                         {"z", pose.rotation.z},
                                         {"w", pose.rotation.w}};
        if (clip.channels & 4)
            c["forge.local_scale"] = {
                {"x", pose.scale.x}, {"y", pose.scale.y}, {"z", pose.scale.z}};
        return true;
    }
    return false;
}
} // namespace forge
