#pragma once
#include "model_instance_pose.hpp"
#include <limits>
inline void check_model_instance_poses() {
    using namespace forge;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto rejects = [&](auto fn) {
        bool caught = false;
        try {
            fn();
        } catch (const std::exception&) {
            caught = true;
        }
        check(caught, "Invalid model instance pose accepted");
    };
    const auto model = AssetId::generate(), mesh_node = AssetId::generate(),
               joint_node = AssetId::generate();
    const auto root_a = EntityId::generate(), root_b = EntityId::generate(),
               mesh_a = EntityId::generate(), mesh_b = EntityId::generate(),
               joint_a = EntityId::generate(), joint_b = EntityId::generate();
    MeshResourceData resource;
    MeshPart part;
    part.vertices = 2;
    part.bounds = {{0, 0, 0}, {1, 1, 1}};
    part.streams = {{"JOINTS_0", 4, std::vector<std::uint32_t>(8)},
                    {"WEIGHTS_0", 4, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0}}};
    part.joint_palette = {0};
    part.morph_targets = {{{"POSITION", 3, std::vector<float>{1, 0, 0, 1, 0, 0}}}};
    resource.mesh.lods = {{1, {part}}};
    resource.mesh.morph_defaults = {0};
    resource.mesh.morph_names = {"Shift"};
    AffineTransform inverse;
    inverse.m[3] = -1;
    resource.model = MeshModelBindings{model,
                                       std::string(64, 'a'),
                                       {{{joint_node}, {inverse}}},
                                       {{mesh_node, 0, {0}, true, true}}};
    const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
    RenderScene scene;
    AffineTransform joint_first, joint_second, singular_mesh;
    joint_first.m[3] = 10;
    joint_second.m[3] = -20;
    singular_mesh.m.fill(0);
    scene.model_nodes = {{root_a, {{model}, {}}, {}, root_a},
                         {root_b, {{model}, {}}, {}, root_b},
                         {mesh_a, {{model}, {mesh_node}}, singular_mesh, root_a},
                         {mesh_b, {{model}, {mesh_node}}, {}, root_b},
                         {joint_a, {{model}, {joint_node}}, joint_first, root_a},
                         {joint_b, {{model}, {joint_node}}, joint_second, root_b}};
    scene.model_animations = {{root_a, model, std::string(64, 'a'), true, {{mesh_node, {-2}}}},
                              {root_b, model, std::string(64, 'a'), true, {{mesh_node, {3}}}}};
    RenderMesh first{mesh_a, {}, singular_mesh, {}}, second{mesh_b, {}, {}, {}};
    auto prepare = [&](const RenderMesh& mesh) {
        return prepare_mesh_instance_pose(resource, geometry, mesh, ModelSceneIndex(scene));
    };
    auto a = prepare(first), b = prepare(second);
    check(a.skinned && b.skinned && a.morph_weights[0] == -2 && b.morph_weights[0] == 3,
          "Per-instance signed morph weights were mixed or clamped");
    check(a.lods[0][0].skin->palette[0] == joint_first * inverse &&
              b.lods[0][0].skin->palette[0] == joint_second * inverse,
          "Required skin joints escaped their model instance or ignored inverse binds");
    const auto bounds_a = mesh_instance_bounds(a, {}), bounds_b = mesh_instance_bounds(b, {});
    check(bounds_a.minimum[0] > 6.99 && bounds_a.maximum[0] < 8.01 &&
              bounds_b.minimum[0] > -18.01 && bounds_b.maximum[0] < -16.99,
          "Morphed/skinned bounds used singular mesh-node transform or another instance");
    const auto retained = a;
    // Replacement admission includes temporary joint data; excluding that data
    // must fail without modifying the previous pose. The exact payload fits.
    const auto pose_bytes = mesh_pose_bytes(a);
    const auto scratch_bytes = sizeof(AffineTransform) + sizeof(AssetId);
    rejects([&] {
        a = prepare_mesh_instance_pose(resource, geometry, first, ModelSceneIndex(scene),
                                       pose_bytes + scratch_bytes - 1);
    });
    check(a == retained, "Payload rejection changed the previous model pose");
    check(prepare_mesh_instance_pose(resource, geometry, first, ModelSceneIndex(scene),
                                     pose_bytes + scratch_bytes) == retained,
          "Exact pose payload admission rejected a fitting candidate");
    const auto geometry_bytes = mesh_pose_bytes(geometry);
    rejects([&] { (void)prepare_mesh_pose_geometry(resource.mesh, geometry_bytes - 1); });
    check(mesh_pose_bytes(prepare_mesh_pose_geometry(resource.mesh, geometry_bytes)) ==
              geometry_bytes,
          "Exact geometry payload admission failed");
    // Budget admission precedes reading malformed delta data or computing a
    // palette. This small fixture tests the ordering without huge allocations.
    auto invalid_geometry = resource.mesh;
    invalid_geometry.lods[0].parts[0].morph_targets[0][0].components = 1;
    try {
        (void)prepare_mesh_pose_geometry(invalid_geometry, 0);
        check(false, "Zero geometry budget accepted an allocation");
    } catch (const std::exception& error) {
        check(std::string(error.what()).find("payload budget") != std::string::npos,
              "Geometry allocation/validation preceded payload admission");
    }
    auto failed_candidate = [&] {
        rejects([&] { a = prepare(first); });
        check(a == retained, "Failed candidate changed the previous complete pose");
    };
    scene.model_animations[0].revision.assign(64, 'b');
    failed_candidate();
    scene.model_animations[0].revision.assign(64, 'a');
    scene.model_animations[0].ready = false;
    failed_candidate();
    scene.model_animations[0].ready = true;
    scene.model_animations[0].morphs[mesh_node] = {std::numeric_limits<float>::infinity()};
    failed_candidate();
    scene.model_animations[0].morphs[mesh_node] = {-2, 1};
    failed_candidate();
    scene.model_animations[0].morphs[mesh_node] = {-2};
    scene.model_nodes[4].root = {};
    failed_candidate(); // Root B still has the same source joint; never borrow it.
    scene.model_nodes[4].root = root_a;
    auto duplicate = scene.model_nodes[4];
    duplicate.entity = EntityId::generate();
    scene.model_nodes.push_back(duplicate);
    failed_candidate();
    scene.model_nodes.pop_back();
    resource.model->skins[0].inverse_bind[0].m[0] = std::numeric_limits<double>::quiet_NaN();
    failed_candidate();
    resource.model->skins[0].inverse_bind[0] = inverse;
    check(prepare(first) == retained, "Repair failed to restore compatible instance pose");
    // The exact same mesh can be referenced by an unskinned node. Attributes
    // alone do not enable skinning; its ordinary node world transform is used.
    resource.model->nodes[0].skin.reset();
    first.world = {};
    first.world.m[3] = 30;
    const auto unskinned = prepare(first);
    const auto ordinary_bounds = mesh_instance_bounds(unskinned, {});
    check(!unskinned.skinned && !unskinned.lods[0][0].skin && ordinary_bounds.minimum[0] > 27.99 &&
              ordinary_bounds.maximum[0] < 29.01,
          "Unskinned model node used joint data or omitted its world transform");
    // Removing an optional animation consumer restores source-node defaults.
    scene.model_animations.clear();
    resource.model->nodes[0].morph_weights = {.5f};
    check(prepare(first).morph_weights == std::vector<float>{.5f},
          "Source-node default morph weights were replaced by mesh defaults");
    // A prepared interval owns no vertex storage and remains usable across
    // weight updates. A later delta edit requires preparing a new revision.
    auto source = resource.mesh.lods[0].parts[0];
    const auto summary = prepare_morph_bounds(source);
    const auto expected = morph_bounds(summary, std::array<float, 1>{-3});
    std::get<std::vector<float>>(source.morph_targets[0][0].values)[0] = 1000;
    check(morph_bounds(summary, std::array<float, 1>{-3}) == expected &&
              morph_bounds(source, std::array<float, 1>{-3}) != expected,
          "Prepared morph bounds re-read mutable source vertices");
}
