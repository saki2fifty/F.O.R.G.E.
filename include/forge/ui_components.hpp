#pragma once
#include <array>
#include <forge/asset_ref.hpp>
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
struct UiSurfaceInput {
    AssetRef<RenderTextureAsset> render_texture;
    bool enabled = true;
    bool operator==(const UiSurfaceInput&) const = default;
};
// Transient host/presenter input value; never registered or persisted as component data.
struct UiSurfaceHit {
    EntityId surface;
    AssetRef<RenderTextureAsset> output;
    std::array<double, 2> uv{};
    EntityId view; // Optional host view identity, retained through a pointer gesture.
};
} // namespace forge
