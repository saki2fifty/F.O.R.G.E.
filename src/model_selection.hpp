#pragma once
#include "model_bundle.hpp"
#include <forge/assets.hpp>
#include <memory>
#include <stop_token>
namespace forge::asset_detail {
// Immutable loaded-container metadata. The catalog remains the binding authority;
// these are checked copies for one selected revision, not another authored graph.
struct ModelSelection {
    AssetId owner;
    std::string revision;
    std::uint64_t generation = 0;
    ModelBundleIndex index;
    std::map<std::string, AssetId> bindings;
    // Derived lookup indices for this immutable selection only, not persisted IDs.
    std::map<AssetId, std::size_t> member_indices;
    std::map<std::string, std::size_t> file_indices;
    std::shared_ptr<const CachedArtifact> artifact;
    const ModelImportMember& member(AssetId id) const;
    std::span<const std::byte> bytes(const ModelImportMember& member) const;
};
// Loads only selected immutable cooked data. No source conversion, editor, codec,
// device, project publication or gameplay-world mutation occurs here.
ModelSelection load_model_selection(const std::filesystem::path& project,
                                    const AssetCatalog& catalog, AssetId model,
                                    std::stop_token stop = {});
} // namespace forge::asset_detail
