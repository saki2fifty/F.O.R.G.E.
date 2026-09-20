#pragma once
#include <forge/texture_asset.hpp>
namespace forge {
// Immutable cooked selection index. One logical Texture AssetId may supply color,
// data and normal variants together; these keys are not new persistent identities.
struct TextureVariantEntry {
    TextureSemantic semantic = TextureSemantic::Color;
    std::string file, digest;
    std::uint64_t bytes = 0;
    bool operator==(const TextureVariantEntry&) const = default;
};
struct TextureBundleIndex {
    TextureSemantic primary = TextureSemantic::Color;
    std::vector<TextureVariantEntry> variants;
    const TextureVariantEntry& find(TextureSemantic semantic) const;
    bool operator==(const TextureBundleIndex&) const = default;
};
std::string_view texture_variant_key(TextureSemantic semantic);
std::string texture_variant_file(TextureSemantic semantic, std::string_view prefix = {});
std::vector<std::byte> encode_texture_bundle_index(const TextureBundleIndex& index);
TextureBundleIndex decode_texture_bundle_index(std::span<const std::byte> bytes);
} // namespace forge
