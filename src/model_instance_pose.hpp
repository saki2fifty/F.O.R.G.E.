#pragma once
#include "mesh_morph.hpp"
#include "render_bounds.hpp"
#include <forge/mesh_resource.hpp>
#include <forge/render_scene.hpp>
#include <set>
namespace forge {
// One bounded snapshot index, reused by every model mesh in this frame. It owns
// no entities and never resolves a source node outside its ordinary model root.
class ModelSceneIndex {
  public:
    explicit ModelSceneIndex(const RenderScene&);
    const RenderModelNode* entity(EntityId) const;
    const RenderModelNode* joint(EntityId root, AssetId node) const;
    const RenderModelAnimation* animation(EntityId root) const;
    void validate_root(EntityId, AssetId) const;

  private:
    std::map<EntityId, const RenderModelNode*> entities_;
    std::map<std::pair<EntityId, AssetId>, const RenderModelNode*> joints_;
    std::map<EntityId, const RenderModelAnimation*> animations_;
    std::set<EntityId> ambiguous_;
};
using MeshPoseGeometry = std::vector<std::vector<MorphBoundsData>>;
// Derived CPU payload profile, independent of authored scale validity and the
// resource pools. Callers include retained values when admitting replacements.
inline constexpr std::uint64_t mesh_pose_payload_limit = 512ull * 1024 * 1024;
MeshPoseGeometry prepare_mesh_pose_geometry(const MeshData&,
                                            std::uint64_t available = mesh_pose_payload_limit);
struct MeshPartPose {
    MeshBounds local_bounds;
    std::optional<SkinPose> skin;
    bool operator==(const MeshPartPose&) const = default;
};
struct MeshInstancePose {
    AffineTransform world;
    bool skinned = false;
    std::vector<float> morph_weights;
    std::vector<std::vector<MeshPartPose>> lods;
    bool operator==(const MeshInstancePose&) const = default;
};
// All instance/binding/weight/bounds checks complete before replacing a retained
// pose or allocating a new physical draw bundle. A failure leaves both untouched.
MeshInstancePose prepare_mesh_instance_pose(const MeshResourceData&, const MeshPoseGeometry&,
                                            const RenderMesh&, const ModelSceneIndex&,
                                            std::uint64_t available = mesh_pose_payload_limit);
std::uint64_t mesh_pose_bytes(const MeshPoseGeometry&);
std::uint64_t mesh_pose_bytes(const MeshInstancePose&);
RenderBounds mesh_part_bounds(const MeshInstancePose&, unsigned lod, unsigned part, Double3 origin);
RenderBounds mesh_instance_bounds(const MeshInstancePose&, Double3 origin);
} // namespace forge
