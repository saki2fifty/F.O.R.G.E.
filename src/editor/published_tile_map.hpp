#pragma once
#include "../mesh_render_host.hpp"
#include "tile_map_inspector.hpp"

namespace forge::ui {
// One selected read-only view, using the existing render host's CPU Map/Set pools.
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
                return {values_, revision_, "Preparing project assets...", false, definitions_};
            if (catalog_ != catalog) {
                // Revalidate dependency revisions even when the Map's own key is unchanged.
                ticket_ = host->request_tile_map_values(asset);
                definition_ticket_ = {};
                catalog_ = catalog;
            }
            host->pump();
            const auto status = ticket_.inspect();
            const auto lease = host->tile_map_values(ticket_);
            if (!lease)
                return {values_, revision_,
                        status.diagnostic.empty() ? "Preparing published Map layers..."
                                                  : status.diagnostic,
                        false, definitions_};
            if (!definition_ticket_)
                definition_ticket_ = host->request_tile_set_values({lease.get().tile_set()});
            const auto definitions = host->tile_set_values(definition_ticket_);
            const auto definition_status = definition_ticket_.inspect();
            const bool current = bool(lease) && bool(definitions) &&
                                 status.state == ResourceState::Ready &&
                                 definition_status.state == ResourceState::Ready;
            if (current) {
                const auto revision =
                    lease.identity().revision + ":" + definitions.identity().revision;
                if (!values_ || revision_ != revision) {
                    values_ = std::make_shared<const TileMapData>(lease.get());
                    definitions_ = std::make_shared<const TileSetData>(definitions.get());
                    revision_ = revision;
                }
                lease_ = lease;
                definition_lease_ = definitions;
            }
            const auto& diagnostic =
                status.diagnostic.empty() ? definition_status.diagnostic : status.diagnostic;
            return {values_, revision_,
                    current              ? "Published Map layers"
                    : diagnostic.empty() ? "Preparing published Map and Tile Set..."
                                         : diagnostic,
                    current, definitions_};
        } catch (const std::exception& error) {
            return {values_, revision_, error.what(), false, definitions_};
        }
    }

  private:
    void reset() {
        host_.reset();
        catalog_.reset();
        asset_ = {};
        revision_.clear();
        ticket_ = {};
        definition_ticket_ = {};
        lease_ = {};
        definition_lease_ = {};
        values_.reset();
        definitions_.reset();
    }
    std::weak_ptr<MeshResourceHost> host_;
    std::shared_ptr<const AssetCatalog> catalog_;
    AssetId asset_;
    std::string revision_;
    ResourceTicket ticket_, definition_ticket_;
    ResourceLease<TileMapAsset> lease_;
    ResourceLease<TileSetAsset> definition_lease_;
    std::shared_ptr<const TileMapData> values_;
    std::shared_ptr<const TileSetData> definitions_;
};
} // namespace forge::ui
