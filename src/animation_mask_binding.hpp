#pragma once
#include "model_selection.hpp"
#include <forge/animation_components.hpp>
#include <map>
namespace forge::asset_detail {
struct AnimationMaskJoints {
    std::vector<AssetId> assets;
    std::vector<std::int16_t> parents;
};
// Disposable indices for one admitted model selection, never persistent bone identity.
inline AnimationMaskJoints animation_mask_joints(const ModelSelection& selected, AssetId skeleton) {
    if (selected.index.version < 3 ||
        selected.member(skeleton).identity.type != SkeletonAsset::type)
        throw std::runtime_error("Animation Mask requires durable imported skeleton metadata");
    const auto& plan = selected.index.hierarchy.at("animation").at("plan");
    const auto nodes = plan.at("joint_nodes").get<std::vector<std::size_t>>();
    AnimationMaskJoints result{{}, plan.at("joint_parents").get<std::vector<std::int16_t>>()};
    std::map<std::size_t, AssetId> assets;
    for (const auto& member : selected.index.members)
        if (member.identity.type == ModelNodeAsset::type && member.node)
            if (!assets.emplace(*member.node, selected.bindings.at(member.identity.address)).second)
                throw std::runtime_error("Animation Mask skeleton node mapping is duplicated");
    for (const auto node : nodes) {
        if (!assets.contains(node))
            throw std::runtime_error("Animation Mask skeleton node identity is unavailable");
        result.assets.push_back(assets.at(node));
    }
    return result;
}
} // namespace forge::asset_detail
