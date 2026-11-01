#pragma once
#include "sprite_atlas_packing.hpp"
#include <forge/identity.hpp>
#include <nlohmann/json.hpp>
#include <span>

namespace forge {
inline constexpr std::size_t sprite_atlas_recipe_byte_limit = 2 * 1024 * 1024;
struct SpriteAtlasInput {
    std::uint32_t id = 0;
    AssetId texture;
    bool operator==(const SpriteAtlasInput&) const = default;
};
// Authored inputs and packing intent only. Generated rectangles belong to the
// immutable cooked Texture revision, never to another source/frame database.
struct SpriteAtlasRecipe {
    nlohmann::json document;
    static SpriteAtlasRecipe create(AssetId, std::span<const AssetId>, SpriteAtlasPacking = {});
    static SpriteAtlasRecipe parse(std::span<const std::byte>);
    AssetId asset() const;
    std::uint64_t next_id() const;
    std::vector<SpriteAtlasInput> inputs() const;
    SpriteAtlasPacking settings() const;
    void validate() const;
    std::uint32_t add_image(AssetId);
    void replace_image(std::uint32_t, AssetId);
    void erase_image(std::uint32_t);
    void reorder(std::span<const std::uint32_t>);
};
std::vector<SpriteFrameEntry> sprite_atlas_frame_entries(const SpriteAtlasRecipe&,
                                                         const SpriteAtlasLayout&);
} // namespace forge
