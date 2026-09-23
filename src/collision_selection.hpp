#pragma once
#include "collision_resource.hpp"
#include <forge/assets.hpp>
namespace forge {
ResourceTicket request_collision(ResourcePool<CollisionAsset>&, std::filesystem::path,
                                 std::shared_ptr<const AssetCatalog>, AssetRef<CollisionAsset>);
}
