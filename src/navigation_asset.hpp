#pragma once
#include "navigation_mesh.hpp"
#include <forge/assets.hpp>
#include <forge/navigation_build.hpp>
namespace forge::navigation_detail {
struct Admitted {
    nlohmann::json metadata;
    std::shared_ptr<const Mesh> mesh;
};
// Validate the envelope's coordinate/provenance contract before native work.
NavigationSettings provenance_settings(const nlohmann::json& metadata);
std::vector<std::byte> envelope(nlohmann::json metadata, std::span<const std::byte> tile);
Admitted admit(std::span<const std::byte> bytes);
Admitted load(const std::filesystem::path& project, const AssetRecord& record);
} // namespace forge::navigation_detail
