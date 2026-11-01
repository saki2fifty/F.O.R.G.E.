#pragma once
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
    bool operator==(const UiDocument&) const = default;
};
} // namespace forge
