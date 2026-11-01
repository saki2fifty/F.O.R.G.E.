#pragma once
#include "asset_source_document.hpp"
#include "sprite_atlas_recipe.hpp"
#include <set>
namespace forge {
struct SpriteAtlasDocumentTraits {
    using Source = SpriteAtlasRecipe;
    static constexpr auto byte_limit = sprite_atlas_recipe_byte_limit;
    static constexpr auto suffix = ".atlas.json";
    static constexpr auto asset_type = TextureAsset::type;
    static Source create(AssetId) {
        throw std::invalid_argument("Select at least one Texture before creating a Sprite atlas");
    }
    static void validate_edit(const Source&, const Source&);
    static void restore_identity_metadata(Source&, const Source&);
};
inline void SpriteAtlasDocumentTraits::validate_edit(const Source& before, const Source& after) {
    if (after.next_id() < before.next_id())
        throw std::invalid_argument("Atlas frame key sequence cannot move backwards");
    std::set<std::uint32_t> previous;
    for (const auto input : before.inputs())
        previous.insert(input.id);
    for (const auto input : after.inputs())
        if (!previous.contains(input.id) && input.id < before.next_id())
            throw std::invalid_argument("Atlas frame key was already allocated");
}
inline void SpriteAtlasDocumentTraits::restore_identity_metadata(Source& restored,
                                                                 const Source& current) {
    restored.document["next_id"] = std::max(restored.next_id(), current.next_id());
}
using SpriteAtlasDocument = AssetSourceDocument<SpriteAtlasDocumentTraits>;
} // namespace forge
