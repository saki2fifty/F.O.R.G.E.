#pragma once
#include "mesh_processing.hpp"
#include <array>
#include <forge/gltf_accessors.hpp>
#include <forge/mesh_asset.hpp>
#include <limits>

namespace Diligent::GLTF {
class Document;
}
namespace tinygltf {
class Model;
}
namespace forge::asset_detail {
void validate_gltf_mesh_containers(const GltfSourceBundle& source);
inline constexpr std::size_t gltf_no_index = std::numeric_limits<std::size_t>::max();
struct NativeGltfNode {
    std::size_t parent = gltf_no_index, root = gltf_no_index;
    std::size_t mesh = gltf_no_index, skin = gltf_no_index, camera = gltf_no_index;
    // Source-space column-major affine matrix; not an authored ECS component.
    // Reflection/zero scale remain intact for explicit import representability policy.
    std::array<double, 16> matrix{};
    std::vector<double> morph_weights;
};
struct NativeGltfSkinBinding {
    std::vector<std::size_t> joints;
    std::size_t common_root = gltf_no_index, skeleton = gltf_no_index;
    std::size_t inverse_bind_accessor = gltf_no_index;
};
struct NativeGltfHierarchy {
    std::vector<NativeGltfNode> nodes;
    std::vector<std::size_t> parent_first;
    std::vector<std::vector<std::size_t>> scenes;
    std::size_t default_scene = gltf_no_index;
    std::vector<NativeGltfSkinBinding> skins;
};
NativeGltfHierarchy validate_gltf_hierarchy(const GltfSourceBundle& source);
template <class T> struct NativeGltfValues {
    std::size_t count = 0;
    unsigned components = 0;
    std::vector<T> values;
};
enum class NativePrimitiveTopology { Points, Lines, Triangles };
struct NativeMeshPrimitive {
    NativePrimitiveTopology topology = NativePrimitiveTopology::Triangles;
    std::size_t vertex_count = 0;
    std::map<std::string, NativeGltfValues<float>> attributes;
    std::map<std::string, NativeGltfValues<std::uint32_t>> integer_attributes;
    std::vector<std::uint32_t> indices;
    std::vector<std::map<std::string, NativeGltfValues<float>>> morph_targets;
    std::array<float, 3> minimum{}, maximum{};
    // Candidate-local material index, resolved to a durable AssetId by publication.
    int material = -1;
    std::vector<std::string> diagnostics;
};
enum class ExcessSkinInfluences { Reject, ReduceToFour };
struct NativeSkinVertex {
    std::array<std::uint16_t, 4> joints{};
    std::array<float, 4> weights{};
};
struct NativeSkinInfluences {
    std::vector<NativeSkinVertex> vertices;
    // Draw-local palette -> ordered source skin.joints index. At most 256 entries.
    std::vector<std::uint32_t> palette;
    std::size_t reduced_vertices = 0, renormalized_vertices = 0;
};
NativeSkinInfluences prepare_gltf_skin_influences(const NativeMeshPrimitive& primitive,
                                                  std::size_t joint_count,
                                                  ExcessSkinInfluences policy);
enum class NativeAnimationPath { Translation, Rotation, Scale, Weights };
enum class NativeAnimationInterpolation { Linear, Step, CubicSpline };
struct NativeAnimationTrack {
    std::size_t node = gltf_no_index;
    NativeAnimationPath path = NativeAnimationPath::Translation;
    NativeAnimationInterpolation interpolation = NativeAnimationInterpolation::Linear;
    unsigned components = 0;
    std::shared_ptr<const std::vector<float>> times;
    // Cubic data retains [in tangent, value, out tangent] for every key.
    std::shared_ptr<const std::vector<float>> values;
};
struct NativeAnimationClip {
    std::vector<NativeAnimationTrack> tracks;
    std::vector<std::string> diagnostics;
    double duration = 0;
};
// Worker-only adapter. Upstream scene objects never become gameplay authority.
// FORGE snapshots and JSON stay on this side of Diligent's JSON_DIAGNOSTICS ABI.
class NativeGltfDocument {
  public:
    explicit NativeGltfDocument(GltfSourceBundle captured);
    ~NativeGltfDocument();
    NativeGltfDocument(const NativeGltfDocument&) = delete;
    NativeGltfDocument& operator=(const NativeGltfDocument&) = delete;
    const tinygltf::Model& model() const;
    const GltfSourceBundle& source() const { return source_; }
    // Derived import scene expansion. Captured source/provenance remains immutable.
    const GltfSourceBundle& scene_source() const { return expanded_ ? *expanded_ : source_; }
    // Encoded image bytes after buffer-view decompression, before image decoding.
    const std::vector<GltfEncodedImage>& encoded_images() const { return images_; }
    const NativeGltfHierarchy& hierarchy() const { return hierarchy_; }
    std::size_t captured_reads() const { return captured_reads_; }
    // Uses the pinned native VertexDataConverter after admission. Sparse patches
    // and matrix column padding are applied explicitly; integer IDs stay exact.
    NativeGltfValues<float> floats(std::size_t accessor) const;
    NativeGltfValues<std::uint32_t> unsigned_integers(std::size_t accessor) const;
    NativeMeshPrimitive primitive(std::size_t mesh, std::size_t primitive) const;
    // Column-major matrices in source joint order; absent source matrices are identity.
    std::vector<std::array<float, 16>> inverse_bind_matrices(std::size_t skin) const;
    NativeAnimationClip animation(std::size_t index) const;

  private:
    GltfSourceBundle source_;
    std::unique_ptr<GltfSourceBundle> expanded_;
    nlohmann::json meshes_; // Admitted private primitive/accessor routing, not source identity.
    std::vector<GltfEncodedImage> images_;
    NativeGltfHierarchy hierarchy_;
    std::unique_ptr<Diligent::GLTF::Document> native_;
    std::size_t captured_reads_ = 0;
};
MeshData cook_gltf_mesh(const NativeGltfDocument& document, std::size_t mesh_index);
ProcessedMesh cook_gltf_mesh(const NativeGltfDocument& document, std::size_t mesh_index,
                             const MeshProcessingOptions& options,
                             ExcessSkinInfluences skin_policy = ExcessSkinInfluences::Reject);
} // namespace forge::asset_detail
