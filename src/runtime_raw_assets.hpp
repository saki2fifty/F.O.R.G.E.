#pragma once
#include <forge/assets.hpp>
namespace forge::package_detail {
bool legacy_animation(const AssetRecord&);
bool ui_source(const AssetRecord&);
bool raw_only(const AssetRecord&);
std::filesystem::path raw_locator(const AssetRecord&);
std::vector<std::byte> admit_raw(const std::filesystem::path&, const AssetRecord&);
void validate_raw_selections(const std::filesystem::path&, const AssetCatalog&);
} // namespace forge::package_detail
