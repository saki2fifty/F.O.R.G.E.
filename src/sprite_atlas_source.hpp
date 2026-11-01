#pragma once
#include "import_process.hpp"
#include "sprite_atlas_settings.hpp"
#include <forge/assets.hpp>
#include <forge/identity.hpp>

namespace forge::asset_detail {
inline constexpr std::string_view sprite_atlas_source_recipe = "forge.texture.atlas-source";
inline constexpr std::string_view sprite_atlas_importer_id = "forge.texture.atlas";
struct SpriteAtlasCapture {
    std::vector<ArtifactFile> images;
    SpriteAtlasLayout layout;
    std::vector<AssetDependency> dependencies;
};
// Shared capture of published independent inputs. Owns no filesystem mutation or
// publication; dependencies identify exactly the pixels used for this layout.
SpriteAtlasCapture capture_sprite_atlas_images(const std::filesystem::path& project,
                                               const AssetCatalog&, std::span<const AssetId>,
                                               SpriteAtlasPacking, std::stop_token = {});
struct SpriteAtlasSource {
    std::vector<std::byte> png;
    // Transient frame mapping, not a new asset or persistent repack recipe.
    nlohmann::json description;
    std::string catalog_digest;
};
std::string sprite_atlas_source_revision();
WorkerLimits sprite_atlas_source_limits();
std::string sprite_atlas_input_name(std::size_t);
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
std::vector<ArtifactFile> execute_sprite_atlas_recipe(const ImportProcessRequest&,
                                                      std::stop_token = {});
} // namespace forge::asset_detail
