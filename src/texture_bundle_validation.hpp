#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/derived_cache.hpp>
#include <forge/texture_bundle.hpp>
#include <set>
namespace forge::asset_detail {
// Shared cooked-format admission for import publication and runtime selection.
inline TextureBundleIndex validate_texture_bundle(std::span<const ArtifactFile> files) {
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto found = std::find_if(files.begin(), files.end(),
                                    [](const auto& f) { return f.name == "texture.json"; });
    require(found != files.end(), "Missing texture bundle index");
    const auto index = decode_texture_bundle_index(found->bytes);
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
        require(decode_texture(file->bytes).semantic == entry.semantic,
                "Texture variant semantic disagrees with bundle");
    }
    return index;
}
} // namespace forge::asset_detail
