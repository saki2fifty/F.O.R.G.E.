#pragma once
#include "asset_bytes.hpp"
#include "texture_bundle_validation.hpp"
#include <algorithm>
#include <filesystem>
#include <forge/asset_importer.hpp>
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/texture_bundle.hpp>
#include <stdexcept>
namespace forge::test {
// Original cooked pixel fixtures share the real immutable publication envelope.
// No image source is needed by consumers of the selected Texture revision.
inline void publish_texture_fixture(const std::filesystem::path& root, AssetCatalog& catalog,
                                    AssetId id, const TextureData& texture, unsigned generation,
                                    ImportTarget target = {"windows", "d3d12", "desktop"}) {
    using namespace asset_detail;
    const auto bytes = encode_texture(texture);
    TextureBundleIndex index;
    index.variants = {{texture.semantic, texture_variant_file(texture.semantic),
                       content_digest(bytes), bytes.size()}};
    AssetBuildInput input;
    input.source_digest = content_digest(bytes);
    input.importer = "forge.texture.fixture";
    input.importer_revision = std::string(64, 'a');
    input.output_format = "forge.texture-bundle";
    input.platform = target.platform;
    input.backend = target.backend;
    input.profile = target.profile;
    const auto artifact = DerivedDataCache(root).publish(
        input,
        {{"texture.json", encode_texture_bundle_index(index)}, {index.variants[0].file, bytes}},
        [](const auto& candidate) { (void)validate_texture_bundle(candidate.files); });
    AssetRecord record;
    record.id = id;
    record.type = "texture";
    record.source = "Assets/" + id.str() + ".texture";
    record.metadata = {{"forge.import",
                        {{"version", 1},
                         {"generation", std::uint64_t(generation)},
                         {"key", input.key()},
                         {"source_digest", input.source_digest},
                         {"importer", input.importer},
                         {"importer_revision", input.importer_revision},
                         {"output_format", input.output_format},
                         {"output_version", 1},
                         {"platform", input.platform},
                         {"backend", input.backend},
                         {"profile", input.profile},
                         {"artifact_digest", asset_build_digest(artifact.manifest.at("files"))}}}};
    if (catalog.records().contains(id))
        catalog.replace(std::move(record));
    else
        catalog.add(std::move(record));
}
// Copy publications only. Editable images are deliberately absent.
inline void published_texture_fixture(const std::filesystem::path& gallery,
                                      const std::filesystem::path& project) {
    const auto source = forge::AssetCatalog::open_project(gallery);
    auto catalog = forge::AssetCatalog::open_project(project);
    for (const char* locator :
         {"Assets/Sprites/Badge.png", "Assets/OcclusionNormals/RippleNormals.png",
          "Assets/ReflectionCapture/Reflection.dds", "Assets/ReflectionProbes/WarmRoom.png",
          "Assets/TexturePrecision/Normalized16.png", "Assets/TextureBlocks/BlockColors.dds",
          "Assets/TextureBlocks/SignedChannel.dds", "Assets/TextureBlocks/SignedPair.dds",
          "Assets/TextureBlocks/HdrUnsigned.dds", "Assets/TextureBlocks/HdrSigned.dds"}) {
        const auto found =
            std::find_if(source.records().begin(), source.records().end(),
                         [&](const auto& row) { return row.second.source == locator; });
        if (found == source.records().end())
            throw std::runtime_error("Published Texture fixture is missing");
        const auto& record = found->second;
        const auto key = record.metadata.at("forge.import").at("key").get<std::string>();
        if (key.size() != 64 || !std::all_of(key.begin(), key.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }))
            throw std::runtime_error("Invalid fixture publication key");
        const auto parent = project / ".forge/cache/derived";
        std::filesystem::create_directories(parent);
        std::filesystem::copy(gallery / ".forge/cache/derived" / key, parent / key,
                              std::filesystem::copy_options::recursive);
        catalog.add(record);
        if (std::filesystem::exists(project / record.source))
            throw std::runtime_error("Source-hidden Texture fixture has editable image bytes");
    }
    catalog.save(project / "forge.assets.json");
}
} // namespace forge::test
