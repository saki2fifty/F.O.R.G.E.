#pragma once
#include <array>
#include <forge/asset_ref.hpp>
#include <optional>
#include <string>
namespace forge {
struct UiLocalizationAsset {
    static constexpr const char* type = "ui_localization";
};
struct UiLocalization {
    AssetRef<UiLocalizationAsset> catalog;
    std::string locale = "en";
    bool operator==(const UiLocalization&) const = default;
};
struct UiDocumentAsset {
    static constexpr const char* type = "ui_document";
};
struct UiDocument {
    AssetRef<UiDocumentAsset> document;
    bool enabled = true, visible = true;
    std::uint32_t layer = 0;
    // Null draws on the main Game display. Camera retains output-image ownership.
    AssetRef<RenderTextureAsset> render_texture{};
    bool operator==(const UiDocument&) const = default;
};
enum class UiSurfaceGeometry : std::uint32_t { Quad = 0, Mesh = 1 };
struct UiSurfaceInput {
    AssetRef<RenderTextureAsset> render_texture;
    bool enabled = true;
    std::uint32_t geometry = std::uint32_t(UiSurfaceGeometry::Quad);
    bool operator==(const UiSurfaceInput&) const = default;
};
// Stable data selection only. No native handle, entity ownership or persistence.
struct UiSurfaceMeshHit {
    AssetId mesh;
    std::string revision, variant;
    std::uint32_t lod = 0, part = 0;
    std::uint64_t primitive = 0;
};
// Transient host/presenter input value; never registered or persisted as component data.
struct UiSurfaceHit {
    EntityId surface;
    AssetRef<RenderTextureAsset> output;
    std::array<double, 2> uv{};
    EntityId view; // Optional host view identity, retained through a pointer gesture.
    std::optional<UiSurfaceMeshHit> mesh{};
};
} // namespace forge
