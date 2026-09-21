#include "model_instance_pose.hpp"
#include <algorithm>
#include <cmath>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
// All arithmetic is checked before requesting vector payload allocations.
struct Payload {
    std::uint64_t available;
    void add(std::uint64_t count, std::uint64_t width) {
        require(width && count <= available / width,
                "Derived mesh pose exceeds the available scene payload budget");
        available -= count * width;
    }
};
} // namespace
ModelSceneIndex::ModelSceneIndex(const RenderScene& scene) {
    for (const auto& node : scene.model_nodes) {
        require(bool(node.entity) && entities_.emplace(node.entity, &node).second,
                "Duplicate or null model entity in presentation snapshot");
        if (node.root && node.source.node.id &&
            !joints_.emplace(std::pair{node.root, node.source.node.id}, &node).second)
            ambiguous_.insert(node.root);
    }
    for (const auto& animation : scene.model_animations)
        if (!animations_.emplace(animation.root, &animation).second)
            ambiguous_.insert(animation.root);
}
const RenderModelNode* ModelSceneIndex::entity(EntityId id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : found->second;
}
const RenderModelNode* ModelSceneIndex::joint(EntityId root, AssetId node) const {
    const auto found = joints_.find({root, node});
    return found == joints_.end() ? nullptr : found->second;
}
const RenderModelAnimation* ModelSceneIndex::animation(EntityId root) const {
    const auto found = animations_.find(root);
    return found == animations_.end() ? nullptr : found->second;
}
void ModelSceneIndex::validate_root(EntityId root, AssetId model) const {
    require(bool(root) && !ambiguous_.contains(root),
            "Model instance has an unresolved root or duplicate source-node binding");
    const auto* found = entity(root);
    require(found && found->root == root && !found->source.node.id &&
                found->source.model.id == model,
            "Model instance root belongs to a different source model");
}
std::uint64_t mesh_pose_bytes(const MeshPoseGeometry& geometry) {
    Payload bytes{UINT64_MAX};
    bytes.add(1, sizeof(geometry));
    bytes.add(geometry.capacity(), sizeof(MeshPoseGeometry::value_type));
    for (const auto& lod : geometry) {
        bytes.add(lod.capacity(), sizeof(MorphBoundsData));
        for (const auto& part : lod)
            bytes.add(part.positions.capacity(), sizeof(std::optional<MeshBounds>));
    }
    return UINT64_MAX - bytes.available;
}
std::uint64_t mesh_pose_bytes(const MeshInstancePose& pose) {
    Payload bytes{UINT64_MAX};
    bytes.add(1, sizeof(pose));
    bytes.add(pose.morph_weights.capacity(), sizeof(float));
    bytes.add(pose.lods.capacity(), sizeof(decltype(pose.lods)::value_type));
    for (const auto& lod : pose.lods) {
        bytes.add(lod.capacity(), sizeof(MeshPartPose));
        for (const auto& part : lod)
            if (part.skin)
                bytes.add(part.skin->palette.capacity(), sizeof(AffineTransform));
    }
    return UINT64_MAX - bytes.available;
}
MeshPoseGeometry prepare_mesh_pose_geometry(const MeshData& mesh, std::uint64_t available) {
    Payload admission{available};
    admission.add(1, sizeof(MeshPoseGeometry));
    admission.add(mesh.lods.size(), sizeof(MeshPoseGeometry::value_type));
    for (const auto& lod : mesh.lods) {
        admission.add(lod.parts.size(), sizeof(MorphBoundsData));
        for (const auto& part : lod.parts) {
            require(part.morph_targets.size() <= 256, "Morph bounds target count exceeds profile");
            admission.add(part.morph_targets.size(), sizeof(std::optional<MeshBounds>));
        }
    }
    MeshPoseGeometry result;
    result.reserve(mesh.lods.size());
    for (const auto& lod : mesh.lods) {
        auto& output = result.emplace_back();
        output.reserve(lod.parts.size());
        for (const auto& part : lod.parts)
            output.push_back(prepare_morph_bounds(part));
    }
    require(mesh_pose_bytes(result) <= available,
            "Reserved mesh geometry exceeds the available scene payload budget");
    return result;
}
MeshInstancePose prepare_mesh_instance_pose(const MeshResourceData& resource,
                                            const MeshPoseGeometry& geometry,
                                            const RenderMesh& mesh, const ModelSceneIndex& index,
                                            std::uint64_t available) {
    MeshInstancePose result;
    result.world = mesh.world;
    std::span<const float> weights = resource.mesh.morph_defaults;
    const MeshSkinBinding* skin = nullptr;
    const RenderModelNode* node = index.entity(mesh.entity);
    if (resource.model && node) {
        const auto& model = *resource.model;
        require(node->source.model.id == model.model && bool(node->source.node.id),
                "Mesh source does not match the selected model node");
        index.validate_root(node->root, model.model);
        const MeshModelNodeBinding* binding = nullptr;
        for (const auto& candidate : model.nodes)
            if (candidate.node == node->source.node.id) {
                require(!binding, "Mesh has duplicate source-node bindings");
                binding = &candidate;
            }
        require(binding, "Selected model node does not bind this mesh revision");
        weights = binding->morph_weights;
        if (binding->skin) {
            require(*binding->skin < model.skins.size(), "Mesh skin binding is out of range");
            skin = &model.skins[*binding->skin];
            result.skinned = true;
        }
        if (const auto* animation = index.animation(node->root)) {
            require(animation->ready, "Model animation is not ready; retaining the previous pose");
            require(animation->model == model.model && animation->revision == model.revision,
                    "Mesh and model animation revisions differ; retaining the previous pose");
            if (const auto found = animation->morphs.find(node->source.node.id);
                found != animation->morphs.end())
                weights = found->second;
        }
    } else if (resource.model) {
        // A standalone static model mesh can be used without source-node scope.
        // Skin data requires an actual instance binding; never guess a skeleton.
        require(std::none_of(resource.model->nodes.begin(), resource.model->nodes.end(),
                             [](const auto& value) { return value.skin.has_value(); }),
                "Skinned model mesh requires a matching model instance node");
    }
    require(weights.size() == resource.mesh.morph_defaults.size() && weights.size() <= 256 &&
                std::all_of(weights.begin(), weights.end(),
                            [](float value) { return std::isfinite(value); }),
            "Model morph weights do not match the finite mesh target profile");
    Payload admission{available};
    admission.add(1, sizeof(MeshInstancePose));
    admission.add(weights.size(), sizeof(float));
    admission.add(resource.mesh.lods.size(), sizeof(decltype(result.lods)::value_type));
    if (skin) {
        require(!skin->joints.empty() && skin->joints.size() <= 32768 &&
                    skin->joints.size() == skin->inverse_bind.size(),
                "Model skin joint and inverse-bind counts differ");
        // Ordered joint matrices and sorted duplicate-check IDs are temporary
        // but coexist with both retained and candidate pose payloads.
        admission.add(skin->joints.size(), sizeof(AffineTransform) + sizeof(AssetId));
    }
    for (const auto& lod : resource.mesh.lods) {
        admission.add(lod.parts.size(), sizeof(MeshPartPose));
        if (skin)
            for (const auto& part : lod.parts) {
                require(!part.joint_palette.empty() && part.joint_palette.size() <= 256,
                        "Skin draw palette exceeds profile");
                admission.add(part.joint_palette.size(), sizeof(AffineTransform));
            }
    }
    result.morph_weights.assign(weights.begin(), weights.end());
    std::vector<AffineTransform> joints;
    std::vector<AssetId> used;
    if (skin) {
        used.assign(skin->joints.begin(), skin->joints.end());
        std::sort(used.begin(), used.end());
        require(std::adjacent_find(used.begin(), used.end()) == used.end(),
                "Model skin has duplicate joints");
        joints.reserve(skin->joints.size());
        for (const auto id : skin->joints) {
            const auto* joint = index.joint(node->root, id);
            require(bool(id) && joint && joint->source.model.id == resource.model->model,
                    "Model skin joint is missing or belongs to another instance");
            joints.push_back(joint->world);
        }
    }
    require(geometry.size() == resource.mesh.lods.size() && !geometry.empty(),
            "Prepared model bounds differ from mesh LODs");
    result.lods.reserve(geometry.size());
    for (std::size_t l = 0; l < geometry.size(); ++l) {
        const auto& source = resource.mesh.lods[l];
        require(geometry[l].size() == source.parts.size() && !source.parts.empty(),
                "Prepared model bounds differ from mesh parts");
        auto& output = result.lods.emplace_back();
        output.reserve(source.parts.size());
        for (std::size_t p = 0; p < source.parts.size(); ++p) {
            MeshPartPose pose;
            pose.local_bounds = morph_bounds(geometry[l][p], result.morph_weights);
            if (skin) {
                require(source.parts[p].find("JOINTS_0") && source.parts[p].find("WEIGHTS_0"),
                        "Skinned model part lacks admitted joint/weight channels");
                pose.skin = prepare_skin_pose(joints, skin->inverse_bind,
                                              source.parts[p].joint_palette, pose.local_bounds);
            } else
                (void)transform_bounds(pose.local_bounds, result.world);
            output.push_back(std::move(pose));
        }
    }
    Payload actual{available};
    actual.add(mesh_pose_bytes(result), 1);
    actual.add(joints.capacity(), sizeof(AffineTransform));
    actual.add(used.capacity(), sizeof(AssetId));
    return result;
}
RenderBounds mesh_part_bounds(const MeshInstancePose& pose, unsigned lod, unsigned part,
                              Double3 origin) {
    const auto& selected = pose.lods.at(lod).at(part);
    return selected.skin ? skin_bounds_for_camera(*selected.skin, origin)
                         : transform_bounds(selected.local_bounds, pose.world);
}
RenderBounds mesh_instance_bounds(const MeshInstancePose& pose, Double3 origin) {
    RenderBounds result;
    bool first = true;
    for (unsigned l = 0; l < pose.lods.size(); ++l)
        for (unsigned p = 0; p < pose.lods[l].size(); ++p) {
            const auto bounds = mesh_part_bounds(pose, l, p, origin);
            for (unsigned c = 0; c < 3; ++c) {
                result.minimum[c] =
                    first ? bounds.minimum[c] : std::min(result.minimum[c], bounds.minimum[c]);
                result.maximum[c] =
                    first ? bounds.maximum[c] : std::max(result.maximum[c], bounds.maximum[c]);
            }
            first = false;
        }
    require(!first, "Model pose has no bounds");
    return result;
}
} // namespace forge
