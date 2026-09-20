#pragma once
#include <forge/derived_cache.hpp>
#include <stop_token>
namespace forge::asset_detail {
// Validate the exact official converter result against the source-derived plan.
// Archive structural admission always precedes native runtime construction.
// No publication, GPU allocation, ECS identity or world mutation occurs here.
void validate_model_animation(const nlohmann::json& metadata, std::span<const ArtifactFile> files,
                              std::stop_token stop = {});
} // namespace forge::asset_detail
