#pragma once
#include "../mesh_render_host.hpp"
#include "tile_map_inspector.hpp"

namespace forge::ui {
// One selected read-only view, using the existing render host's CPU Map pool.
// The copied value shares immutable chunks and survives project/provider retirement.
class PublishedTileMapInspection {
  public:
    PublishedTileMapView select(std::shared_ptr<MeshResourceHost> host,
                                AssetRef<TileMapAsset> asset) {
        try {
            if (!host || !asset.id) {
                reset();
                return {{}, {}, "Assign a published Tile Map.", false};
            }
            if (host_.lock() != host || asset_ != asset.id) {
                reset();
                host_ = host;
                asset_ = asset.id;
            }
            const auto catalog = host->catalog();
            if (!catalog)
                return {values_, revision_, "Preparing project assets...", false};
            if (catalog_ != catalog) {
                const auto found = catalog->records().find(asset.id);
                if (found == catalog->records().end() || found->second.type != TileMapAsset::type)
                    throw std::runtime_error("The selected Tile Map is unavailable.");
                const auto key = found->second.metadata.at("forge.import").dump();
                if (key_ != key) {
                    ticket_ = host->request_tile_map_values(asset);
                    key_ = key;
                }
                catalog_ = catalog;
            }
            host->pump();
            const auto status = ticket_.inspect();
            const auto lease = host->tile_map_values(ticket_);
            if (lease && (!values_ || revision_ != lease.identity().revision)) {
                values_ = std::make_shared<const TileMapData>(lease.get());
                revision_ = lease.identity().revision;
            }
            if (lease)
                lease_ = lease;
            const bool current = bool(lease) && status.state == ResourceState::Ready;
            return {values_, revision_,
                    current                     ? "Published Map layers"
                    : status.diagnostic.empty() ? "Preparing published Map layers..."
                                                : status.diagnostic,
                    current};
        } catch (const std::exception& error) {
            return {values_, revision_, error.what(), false};
        }
    }

  private:
    void reset() {
        host_.reset();
        catalog_.reset();
        asset_ = {};
        key_.clear();
        revision_.clear();
        ticket_ = {};
        lease_ = {};
        values_.reset();
    }
    std::weak_ptr<MeshResourceHost> host_;
    std::shared_ptr<const AssetCatalog> catalog_;
    AssetId asset_;
    std::string key_, revision_;
    ResourceTicket ticket_;
    ResourceLease<TileMapAsset> lease_;
    std::shared_ptr<const TileMapData> values_;
};
} // namespace forge::ui
