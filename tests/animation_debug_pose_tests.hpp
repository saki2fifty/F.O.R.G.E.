#pragma once
#include "animation_debug_pose.hpp"
inline void check_animation_debug_poses() {
    using namespace forge;
    auto check = [](bool ok, const char* text) {
        if (!ok)
            throw std::runtime_error(text);
    };
    const auto root = EntityId::generate(), joint = EntityId::generate();
    const auto asset = AssetId::generate(), node = AssetId::generate();
    RenderScene scene;
    AffineTransform matrix;
    matrix.m[3] = 1e12 + 3;
    matrix.m[7] = -8;
    scene.model_nodes = {{root, {{asset}, {}}, {}, root}, {joint, {{asset}, {node}}, matrix, root}};
    scene.model_animations = {{root, asset, std::string(64, 'a'), true, {}}};
    auto source = nlohmann::json{
        {"entities",
         nlohmann::json::array(
             {{{"id", root},
               {"spatial_resolved", true},
               {"components", {{"forge.model_source", {{"model", asset}, {"node", nullptr}}}}},
               {"animation_pose",
                {{"joint_assets", {node}},
                 {"parents", {-1}},
                 {"model", nlohmann::json::array()}}}}})}};
    auto poses = prepare_animation_debug(source, &scene);
    check(poses.size() == 1 && poses[0].positions[0] == std::optional<Double3>({1e12 + 3, -8, 0}),
          "Model bone overlay ignored actual joint world or lost double precision");
    scene.model_nodes[1].root = EntityId::generate();
    poses = prepare_animation_debug(source, &scene);
    check(poses.size() == 1 && !poses[0].positions[0],
          "Debug bones borrowed a joint from another root");
    scene.model_nodes[1].root = root;
    auto duplicate = scene.model_nodes[1];
    duplicate.entity = EntityId::generate();
    scene.model_nodes.push_back(duplicate);
    check(prepare_animation_debug(source, &scene).empty(),
          "Ambiguous debug model binding was drawn");
    scene.model_nodes.pop_back();
    scene.model_animations[0].ready = false;
    check(prepare_animation_debug(source, &scene).empty(),
          "Unavailable model animation drew stale bones");
    check(prepare_animation_debug(source, nullptr).empty(),
          "Missing model snapshot fell back to raw Ozz pose");
    // Existing animation-only inspection still transforms Ozz joint translations
    // by its owner; the model-node path above must not do this a second time.
    auto& row = source["entities"][0];
    row["components"] = nlohmann::json::object();
    row["world_affine"] = AffineTransform{}.m;
    row["world_affine"][3] = 10;
    std::array<double, 16> local{};
    local[0] = local[5] = local[10] = local[15] = 1;
    local[12] = 2;
    row["animation_pose"]["model"] = {local};
    poses = prepare_animation_debug(source, nullptr);
    check(poses.size() == 1 && poses[0].positions[0] == std::optional<Double3>({12, 0, 0}),
          "Legacy standalone debug bone no longer uses its owner transform");
    row["animation_pose"]["parents"] = {0};
    check(prepare_animation_debug(source, nullptr).empty(), "Invalid debug parent was accepted");
    row["animation_pose"] = nullptr;
    check(prepare_animation_debug(source, nullptr).empty(), "Null pose was not omitted safely");
}
