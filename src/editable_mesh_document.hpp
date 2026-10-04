#pragma once
#include "asset_source_document.hpp"
#include <forge/editable_mesh.hpp>
namespace forge {
struct EditableMeshDocumentTraits {
    using Source = EditableMeshSource;
    static constexpr auto byte_limit = editable_mesh_source_byte_limit;
    static constexpr auto asset_type = MeshAsset::type;
    static constexpr auto suffix = ".mesh.json";
    static Source create(AssetId id) { return Source::create_cube(id); }
};
using EditableMeshDocument = AssetSourceDocument<EditableMeshDocumentTraits>;
} // namespace forge
