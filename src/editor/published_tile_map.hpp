#pragma once
#include "../mesh_render_host.hpp"
#include "tile_map_inspector.hpp"

namespace forge::ui {
// Bounded read-only selections using the existing render host's CPU Map/Set pools.
// The copied value shares immutable chunks and survives project/provider retirement.
class PublishedTileMapInspection {
  public:
    PublishedTileMapView select(std::shared_ptr<MeshResourceHost> host,
                                AssetRef<TileMapAsset> asset) {
        if (host_.lock() != host) {
            selections_.clear();
            host_ = host;
        }
        if (!host || !asset.id)
            return {{}, {}, "Assign a published Tile Map.", false};
        if (!selections_.contains(asset.id) && selections_.size() >= 32) {
            const auto oldest = std::min_element(
                selections_.begin(), selections_.end(),
                [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            selections_.erase(oldest);
        }
        auto& selection = selections_[asset.id];
        selection.used = ++use_;
        return selection.select(host, asset);
    }

  private:
    struct Selection {
        std::uint64_t used = 0;
        PublishedTileMapView select(std::shared_ptr<MeshResourceHost> host,
                                    AssetRef<TileMapAsset> asset) {
            try {
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
                        variants_ = resolve_tile_variants(values_, definitions_);
                        revision_ = revision;
                    }
                    lease_ = lease;
                    definition_lease_ = definitions;
                }
                const auto& diagnostic =
                    status.diagnostic.empty() ? definition_status.diagnostic : status.diagnostic;
                return {values_,
                        revision_,
                        current              ? "Published Map layers"
                        : diagnostic.empty() ? "Preparing published Map and Tile Set..."
                                             : diagnostic,
                        current,
                        definitions_,
                        variants_};
            } catch (const std::exception& error) {
                return {values_, revision_, error.what(), false, definitions_};
            }
        }

      private:
        std::shared_ptr<const AssetCatalog> catalog_;
        std::string revision_;
        ResourceTicket ticket_, definition_ticket_;
        ResourceLease<TileMapAsset> lease_;
        ResourceLease<TileSetAsset> definition_lease_;
        std::shared_ptr<const TileMapData> values_;
        std::shared_ptr<const TileSetData> definitions_;
        std::shared_ptr<const TileMapVariants> variants_;
    };
    std::weak_ptr<MeshResourceHost> host_;
    std::map<AssetId, Selection> selections_;
    std::uint64_t use_ = 0;
};
} // namespace forge::ui
