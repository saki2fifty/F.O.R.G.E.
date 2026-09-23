#pragma once
#include "asset_source_document.hpp"
#include <forge/collision_source.hpp>
namespace forge {
struct CollisionDocumentTraits {
    using Source = CollisionSource;
    static constexpr std::size_t byte_limit = 1024 * 1024;
    static constexpr auto asset_type = CollisionAsset::type;
    static constexpr auto suffix = ".collision.json";
    static Source create(AssetId id) { return Source::create(id, CollisionKind::Box); }
};
using CollisionDocument = AssetSourceDocument<CollisionDocumentTraits>;
} // namespace forge
