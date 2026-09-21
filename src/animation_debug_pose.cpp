#include "animation_debug_pose.hpp"
#include "model_instance_pose.hpp"
#include <forge/geometry.hpp>
namespace forge {
std::vector<AnimationDebugSkeleton> prepare_animation_debug(const Json& document,
                                                            const RenderScene* scene) {
    const auto& entities = document.at("entities");
    if (!entities.is_array() || entities.size() > 10000)
        throw std::runtime_error("Animation debug snapshot exceeds the scene profile");
    std::optional<ModelSceneIndex> model;
    if (scene)
        model.emplace(*scene);
    std::vector<AnimationDebugSkeleton> result;
    std::size_t remaining = 8192;
    for (const auto& item : entities) {
        try {
            if (!item.contains("animation_pose") || !item.value("spatial_resolved", true))
                continue;
            const auto& pose = item.at("animation_pose");
            if (!pose.is_object())
                continue;
            const auto& parents = pose.at("parents");
            if (!parents.is_array() || parents.size() > 1024 || parents.size() > remaining)
                continue;
            remaining -= parents.size();
            AnimationDebugSkeleton output;
            output.owner = item.at("id").get<EntityId>();
            for (const auto& parent : parents) {
                if (!parent.is_number_integer() || parent.get<std::int64_t>() < -1 ||
                    parent.get<std::int64_t>() >= std::int64_t(output.parents.size()))
                    throw std::runtime_error("Invalid debug skeleton parent");
                output.parents.push_back(parent.get<int>());
            }
            if (item.at("components").contains("forge.model_source")) {
                // Missing or incompatible instance binding is already diagnosed
                // by runtime/render extraction. Never substitute a rest skeleton.
                if (!model)
                    continue;
                const auto* animation = model->animation(output.owner);
                if (!animation || !animation->ready)
                    continue;
                model->validate_root(output.owner, animation->model);
                const auto& assets = pose.at("joint_assets");
                if (!assets.is_array() || assets.size() != parents.size())
                    continue;
                for (const auto& asset : assets) {
                    const auto* joint = model->joint(output.owner, asset.get<AssetId>());
                    output.positions.push_back(
                        joint ? std::optional<Double3>(joint->world.point({})) : std::nullopt);
                }
            } else {
                const auto& matrices = pose.at("model");
                if (!matrices.is_array() || matrices.size() != parents.size())
                    continue;
                const ObjectTransform owner(item);
                for (const auto& matrix : matrices) {
                    if (!matrix.is_array() || matrix.size() != 16) {
                        output.positions.push_back({});
                        continue;
                    }
                    const Double3 local{matrix.at(12).get<double>(), matrix.at(13).get<double>(),
                                        matrix.at(14).get<double>()};
                    output.positions.push_back(owner.matrix().point(local));
                }
            }
            for (auto& point : output.positions)
                if (point && !std::all_of(point->begin(), point->end(),
                                          [](double v) { return std::isfinite(v); }))
                    point.reset();
            result.push_back(std::move(output));
        } catch (const std::exception&) {
            // Optional diagnostics must not terminate the editor for malformed or
            // obsolete runtime data. The main snapshot/resource boundary reports it.
        }
    }
    return result;
}
} // namespace forge
