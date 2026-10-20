#pragma once
#include "../tile_map_cook.hpp"
#include "../tile_set_cook.hpp"
#include "tile_map_preview.hpp"

namespace forge {
// Source preview reuses one ordinary cell draw. Its derived map identity is
// transient and never registered, saved or exposed as a project asset.
class TileSetPreview {
  public:
    TileSetPreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                   std::filesystem::path project, AssetId source)
        : source_(source),
          canvas_(presentation, context, std::move(project), AssetId::generate(), source) {}
    const std::filesystem::path& project() const { return canvas_.project(); }
    AssetId asset() const { return source_; }
    void draw(const TileSetSource& source, std::uint64_t revision,
              std::shared_ptr<const AssetCatalog> catalog, std::uint64_t selected) {
        if (source.asset() != source_ || !revision)
            throw std::runtime_error("Tile Set preview source changed");
        if (revision_ != revision) {
            auto definitions = std::make_shared<const TileSetData>(
                decode_cooked_tile_set(cook_tile_set(source), source_));
            definitions_ = std::move(definitions);
            revision_ = revision;
            selected_ = 0;
        }
        if (definitions_->tiles().empty()) {
            ImGui::TextUnformatted("Add an image and a tile to preview its frame.");
            return;
        }
        const auto found = std::find_if(definitions_->tiles().begin(), definitions_->tiles().end(),
                                        [=](const auto& tile) { return tile.id == selected; });
        const auto& tile =
            found == definitions_->tiles().end() ? definitions_->tiles().front() : *found;
        if (!cells_ || selected_ != tile.id) {
            auto value = TileMapValue::create(canvas_.asset(), source_);
            const std::array<TileMapCellEdit, 1> edit{{{1, {0, 0}, {tile.id, 0}}}};
            value = value.paint(edit);
            auto cells = std::make_shared<const TileMapData>(
                decode_cooked_tile_map(cook_tile_map(value), value.asset()));
            cells_ = std::move(cells);
            selected_ = tile.id;
            canvas_.frame_cells();
        }
        ImGui::TextWrapped("Preview: %s (one map cell)", tile.name.c_str());
        (void)canvas_.draw(cells_, definitions_, std::move(catalog), false, false, 180);
    }

  private:
    AssetId source_;
    TileMapPreview canvas_;
    std::shared_ptr<const TileSetData> definitions_;
    std::shared_ptr<const TileMapData> cells_;
    std::uint64_t revision_ = 0, selected_ = 0;
};
} // namespace forge
