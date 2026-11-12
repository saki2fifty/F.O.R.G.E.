#pragma once
#include <forge/assets.hpp>
#include <optional>

namespace forge {
// Optional literal adapter for the picker; custom decorator CSS stays source text.
std::optional<AssetId> read_ui_material_style(std::string_view);
// Resolve the selected published Material through the existing cooked loader.
// This observes no GPU readiness and declares no runtime dependency.
nlohmann::json ui_material_reference(const AssetCatalog&, AssetId);
} // namespace forge
