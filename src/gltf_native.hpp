#pragma once
#include <forge/gltf_accessors.hpp>

namespace Diligent::GLTF {
class Document;
}
namespace tinygltf {
class Model;
}
namespace forge::asset_detail {
template <class T> struct NativeGltfValues {
    std::size_t count = 0;
    unsigned components = 0;
    std::vector<T> values;
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
    std::size_t captured_reads() const { return captured_reads_; }
    // Uses the pinned native VertexDataConverter after admission. Sparse patches
    // and matrix column padding are applied explicitly; integer IDs stay exact.
    NativeGltfValues<float> floats(std::size_t accessor) const;
    NativeGltfValues<std::uint32_t> unsigned_integers(std::size_t accessor) const;

  private:
    GltfSourceBundle source_;
    std::unique_ptr<Diligent::GLTF::Document> native_;
    std::size_t captured_reads_ = 0;
};
} // namespace forge::asset_detail
