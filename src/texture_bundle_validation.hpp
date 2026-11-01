#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/derived_cache.hpp>
#include <forge/texture_bundle.hpp>
#include <set>
namespace forge::asset_detail {
// Required publication profile, independent of the bundle index envelope. A
// frame-bearing Texture is never admitted under the ordinary legacy profile.
inline unsigned texture_output_profile(const nlohmann::json& metadata) {
    if (!metadata.is_object() || !metadata.at("version").is_number_integer() ||
        metadata.at("version") != 1 || metadata.at("output_format") != "forge.texture-bundle" ||
        !metadata.at("output_version").is_number_integer() ||
        (metadata.at("output_version") != 1 && metadata.at("output_version") != 2) ||
        ((metadata.at("output_version") == 2) !=
         (metadata.at("importer") == "forge.texture.atlas")))
        throw std::runtime_error("Unsupported selected texture artifact profile");
    return metadata.at("output_version").get<unsigned>();
}
// Shared cooked-format admission for import publication and runtime selection.
inline TextureBundleIndex validate_texture_bundle(std::span<const ArtifactFile> files,
                                                  TextureLimits limits = {},
                                                  unsigned output_version = 1) {
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto found = std::find_if(files.begin(), files.end(),
                                    [](const auto& f) { return f.name == "texture.json"; });
    require(found != files.end(), "Missing texture bundle index");
    const auto index = decode_texture_bundle_index(found->bytes);
    require(output_version == 1 || output_version == 2, "Unsupported texture output profile");
    require(output_version != 2 ||
                (index.primary == TextureSemantic::Color && index.variants.size() == 1),
            "Sprite atlas requires exactly one Color variant");
    if (output_version == 2) {
        limits.dimension = std::min(limits.dimension, 4096u);
        limits.bytes = std::min<std::size_t>(limits.bytes, 64ull * 1024 * 1024);
    }
    require(files.size() == index.variants.size() + 1, "Texture bundle has unexpected file count");
    std::set<std::string> names;
    for (const auto& file : files)
        require(names.insert(file.name).second, "Duplicate texture bundle file");
    for (const auto& entry : index.variants) {
        const auto file = std::find_if(files.begin(), files.end(),
                                       [&](const auto& f) { return f.name == entry.file; });
        require(file != files.end() && file->bytes.size() == entry.bytes &&
                    content_digest(file->bytes) == entry.digest,
                "Texture variant file/digest disagrees with bundle");
        const auto texture = decode_texture(file->bytes, limits);
        require(texture.semantic == entry.semantic,
                "Texture variant semantic disagrees with bundle");
        require(texture.sprite_frames.empty() == (output_version == 1),
                "Texture frame metadata differs from its required output profile");
        require(output_version != 2 || texture.mips == 1,
                "Sprite atlas requires base pixels with frame-local mip generation");
    }
    return index;
}
} // namespace forge::asset_detail
