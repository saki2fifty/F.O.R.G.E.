#include "model_instance_pose.hpp"
#include <algorithm>
#include <cmath>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
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
MeshPoseGeometry prepare_mesh_pose_geometry(const MeshData& mesh) {
    MeshPoseGeometry result;
    for (const auto& lod : mesh.lods) {
        auto& output = result.emplace_back();
        for (const auto& part : lod.parts)
            output.push_back(prepare_morph_bounds(part));
    }
    return result;
}
MeshInstancePose prepare_mesh_instance_pose(const MeshResourceData& resource,
                                            const MeshPoseGeometry& geometry,
                                            const RenderMesh& mesh, const ModelSceneIndex& index) {
    MeshInstancePose result;
    result.world = mesh.world;
    result.morph_weights = resource.mesh.morph_defaults;
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
        result.morph_weights = binding->morph_weights;
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
                result.morph_weights = found->second;
        }
    } else if (resource.model) {
        // A standalone static model mesh can be used without source-node scope.
        // Skin data requires an actual instance binding; never guess a skeleton.
        require(std::none_of(resource.model->nodes.begin(), resource.model->nodes.end(),
                             [](const auto& value) { return value.skin.has_value(); }),
                "Skinned model mesh requires a matching model instance node");
    }
    require(result.morph_weights.size() == resource.mesh.morph_defaults.size() &&
                std::all_of(result.morph_weights.begin(), result.morph_weights.end(),
                            [](float value) { return std::isfinite(value); }),
            "Model morph weights do not match the finite mesh target profile");
    std::vector<AffineTransform> joints;
    if (skin) {
        require(!skin->joints.empty() && skin->joints.size() <= 32768 &&
                    skin->joints.size() == skin->inverse_bind.size(),
                "Model skin joint and inverse-bind counts differ");
        std::set<AssetId> used;
        for (const auto id : skin->joints) {
            const auto* joint = index.joint(node->root, id);
            require(bool(id) && used.insert(id).second && joint &&
                        joint->source.model.id == resource.model->model,
                    "Model skin joint is missing, duplicated or belongs to another instance");
            joints.push_back(joint->world);
        }
    }
    require(geometry.size() == resource.mesh.lods.size() && !geometry.empty(),
            "Prepared model bounds differ from mesh LODs");
    for (std::size_t l = 0; l < geometry.size(); ++l) {
        const auto& source = resource.mesh.lods[l];
        require(geometry[l].size() == source.parts.size() && !source.parts.empty(),
                "Prepared model bounds differ from mesh parts");
        auto& output = result.lods.emplace_back();
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
