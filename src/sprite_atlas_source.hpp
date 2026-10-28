#pragma once
#include "import_process.hpp"
#include "sprite_atlas_packing.hpp"
#include <forge/identity.hpp>

namespace forge::asset_detail {
inline constexpr std::string_view sprite_atlas_source_recipe = "forge.texture.atlas-source";
struct SpriteAtlasSource {
    std::vector<std::byte> png;
    // Transient frame mapping, not a new asset or persistent repack recipe.
    nlohmann::json description;
    std::string catalog_digest;
};
std::string sprite_atlas_source_revision();
WorkerLimits sprite_atlas_source_limits();
std::string sprite_atlas_input_name(std::size_t);
nlohmann::json sprite_atlas_settings(const SpriteAtlasPacking&);
SpriteAtlasPacking sprite_atlas_settings(const nlohmann::json&);
nlohmann::json sprite_atlas_layout_document(const SpriteAtlasLayout&);
// Reads published independent texture selections only. No source parser,
// native image codec, file mutation or catalog publication runs in the parent.
SpriteAtlasSource prepare_sprite_atlas_source(const std::filesystem::path& worker,
                                              const std::filesystem::path& project,
                                              std::span<const AssetId> textures, SpriteAtlasPacking,
                                              std::stop_token = {});
// Fixed isolated worker operation. Pixels are copied with the same pure owner;
// native PNG encoding stays inside the established supervised asset worker.
std::vector<ArtifactFile> execute_sprite_atlas_source(const ImportProcessRequest&,
                                                      std::stop_token = {});
} // namespace forge::asset_detail
