#pragma once
#include <forge/assets.hpp>
namespace forge {
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
