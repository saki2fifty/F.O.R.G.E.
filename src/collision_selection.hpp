#pragma once
#include "collision_resource.hpp"
#include <forge/assets.hpp>
namespace forge {
// Current cooked selection metadata only; no source IO or native shape preparation.
std::string selected_collision_revision(const AssetCatalog&, AssetRef<CollisionAsset>);
ResourceTicket request_collision(ResourcePool<CollisionAsset>&, std::filesystem::path,
                                 std::shared_ptr<const AssetCatalog>, AssetRef<CollisionAsset>);
} // namespace forge
