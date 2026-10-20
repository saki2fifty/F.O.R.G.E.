#pragma once
#include "tile_map_document.hpp"
#include "tile_set_data.hpp"
#include <optional>
#include <set>

namespace forge {
// One temporary gesture, using the existing document's source/history authority.
// The caller owns pointer capture and discards this object on Escape/focus loss.
class TileMapPaintStroke {
  public:
    TileMapPaintStroke(TileMapDocument& document, const TileSetData& definitions,
                       std::uint64_t layer, TileMapCell value)
        : TileMapPaintStroke(document, layer, value) {
        if (base_.document.tile_set() != definitions.asset())
            throw std::invalid_argument("Paint brush has a different Tile Set");
        if (value.tile)
            (void)definitions.tile(value.tile);
    }
    // Erasing must also repair maps whose Tile Set is missing or cannot load.
    TileMapPaintStroke(TileMapDocument& document, std::uint64_t layer)
        : TileMapPaintStroke(document, layer, {}) {}
    const TileMapSource& preview() const { return active_ ? preview_ : base_; }
    std::size_t cell_count() const { return touched_.size(); }
    // The latest accepted segment also updates the derived pure render view.
    std::span<const TileMapCellEdit> delta() const { return delta_; }
    // False means the bounded stroke is full. Keep the previous valid preview
    // and let the user release it before beginning another stroke.
    bool sample(TileCellAddress at) {
        check();
        delta_.clear();
        auto from = previous_.value_or(at), end = at;
        // Resolve raster ties consistently when the pointer reverses direction.
        if (end < from)
            std::swap(from, end);
        const auto dx = std::abs(std::int64_t(end.x) - from.x);
        const auto dy = std::abs(std::int64_t(end.y) - from.y);
        const auto count = std::max(dx, dy) + 1;
        if (count > std::int64_t(tile_map_edit_limit))
            return false;
        std::vector<TileMapCellEdit> writes;
        writes.reserve(std::size_t(count));
        std::int64_t x = from.x, y = from.y, error = dx - dy;
        const int step_x = end.x > from.x ? 1 : -1, step_y = end.y > from.y ? 1 : -1;
        std::size_t added = 0;
        while (true) {
            const TileCellAddress point{std::int32_t(x), std::int32_t(y)};
            writes.push_back({layer_, point, value_});
            added += !touched_.contains(point);
            if (x == end.x && y == end.y)
                break;
            const auto twice = error * 2;
            if (twice > -dy) {
                error -= dy;
                x += step_x;
            }
            if (twice < dx) {
                error += dx;
                y += step_y;
            }
        }
        if (added > tile_map_edit_limit - touched_.size())
            return false;
        TileMapSource next{preview_.document.paint(writes)};
        if (!document_.retains_history(next))
            return false;
        // No authored mutation occurs before commit. If allocation fails while
        // tracking a segment, invalidate the gesture so it cannot be committed.
        try {
            for (const auto& write : writes)
                touched_.insert(write.address);
        } catch (...) {
            active_ = false;
            throw;
        }
        preview_ = std::move(next);
        delta_ = std::move(writes);
        previous_ = at;
        return true;
    }
    void commit() {
        check();
        document_.edit_source(revision_, value_.tile ? "Paint tiles" : "Erase tiles",
                              [&](auto& candidate) { candidate = preview_; });
        active_ = false;
        delta_.clear();
    }
    void cancel() {
        active_ = false;
        delta_.clear();
    }

  private:
    TileMapPaintStroke(TileMapDocument& document, std::uint64_t layer, TileMapCell value)
        : document_(document), revision_(document.revision()), base_(document.source()),
          preview_(base_), layer_(layer), value_(value) {
        if (value.flags > 7 || (!value.tile && value.flags) ||
            std::none_of(base_.document.layers().begin(), base_.document.layers().end(),
                         [=](const auto& row) { return row.id == layer; }))
            throw std::invalid_argument("Paint brush has an invalid layer or cell");
    }
    TileMapDocument& document_;
    std::uint64_t revision_;
    TileMapSource base_, preview_;
    std::uint64_t layer_;
    TileMapCell value_;
    std::optional<TileCellAddress> previous_;
    std::set<TileCellAddress> touched_;
    std::vector<TileMapCellEdit> delta_;
    bool active_ = true;
    void check() const {
        if (!active_ || document_.revision() != revision_)
            throw std::runtime_error("Tile paint gesture is closed or its source has changed");
    }
};
} // namespace forge
