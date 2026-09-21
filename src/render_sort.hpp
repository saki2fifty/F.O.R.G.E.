#pragma once
#include <algorithm>
#include <cmath>
#include <forge/material_asset.hpp>
#include <forge/transform.hpp>
#include <span>
#include <tuple>
namespace forge {
// Ephemeral presentation key. Identity is used only for deterministic ties and
// locality; sorting never changes entity/resource identity or authored order.
struct RenderSortKey {
    MaterialAlpha alpha{};
    TransformParity parity{};
    AssetId material, mesh;
    double depth{};
    EntityId entity;
    unsigned part{};
    bool operator==(const RenderSortKey&) const = default;
};
inline bool render_key_less(const RenderSortKey& a, const RenderSortKey& b) {
    if (a.alpha != b.alpha)
        return a.alpha < b.alpha;
    if (a.alpha == MaterialAlpha::Blend && a.depth != b.depth)
        return a.depth > b.depth;
    return std::tie(a.parity, a.material, a.mesh, a.part, a.entity) <
           std::tie(b.parity, b.material, b.mesh, b.part, b.entity);
}
inline void validate_render_key(const RenderSortKey& key) {
    if (!std::isfinite(key.depth) || key.alpha > MaterialAlpha::Blend)
        throw std::runtime_error("Render sort requires a finite depth and known alpha mode");
}
} // namespace forge
