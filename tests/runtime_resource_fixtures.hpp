#pragma once
#include "texture_bundle_validation.hpp"
#include <forge/assets.hpp>
#include <forge/shader_asset.hpp>
namespace forge::test {
inline AssetId runtime_texture_fixture(const std::filesystem::path& root, AssetCatalog& catalog) {
    using namespace asset_detail;
    std::vector<ArtifactFile> files;
    TextureBundleIndex index;
    for (auto semantic : {TextureSemantic::Color, TextureSemantic::Data, TextureSemantic::Normal,
                          TextureSemantic::HdrColor}) {
        TextureData texture;
        texture.width = texture.height = 1;
        texture.semantic = semantic;
        texture.format = semantic == TextureSemantic::HdrColor ? TextureFormat::RGBA16Float
                         : semantic == TextureSemantic::Color  ? TextureFormat::RGBA8Srgb
                                                               : TextureFormat::RGBA8;
        texture.subresources = {std::vector<std::byte>(texture_layout(texture, 0).bytes)};
        auto bytes = encode_texture(texture);
        index.variants.push_back(
            {semantic, texture_variant_file(semantic), content_digest(bytes), bytes.size()});
        files.push_back({texture_variant_file(semantic), std::move(bytes)});
    }
    files.push_back({"texture.json", encode_texture_bundle_index(index)});
    AssetBuildInput input;
    input.source_digest = content_digest(files.back().bytes);
    input.importer = "forge.texture.fixture";
    input.importer_revision = std::string(64, 'a');
    input.output_format = "forge.texture-bundle";
    input.platform = "linux";
    input.backend = "none";
    input.profile = "cpu";
    auto artifact = DerivedDataCache(root).publish(
        input, files, [](const auto& a) { (void)validate_texture_bundle(a.files); });
    const auto id = AssetId::generate();
    catalog.add({id,
                 "texture",
                 "surface.texture",
                 1,
                 {},
                 {{"forge.import",
                   {{"version", 1},
                    {"generation", std::uint64_t(1)},
                    {"key", input.key()},
                    {"source_digest", input.source_digest},
                    {"importer", input.importer},
                    {"importer_revision", input.importer_revision},
                    {"output_format", input.output_format},
                    {"output_version", 1},
                    {"artifact_digest", asset_build_digest(artifact.manifest.at("files"))}}}}});
    return id;
}
inline AssetId runtime_shader_fixture(const std::filesystem::path& root, AssetCatalog& catalog) {
    // CPU-only transport fixture, following shader_pipeline_tests. This byte is
    // opaque test data and is never submitted to any native graphics API.
    ShaderData shader{
        std::string(64, 'b'),
        std::string(64, 'c'),
        true,
        false,
        {{ShaderStage::Vertex,
          "main",
          {std::byte{1}},
          {{"stage", "vertex"}, {"threads", {0, 0, 0}}, {"resources", nlohmann::json::array()}}}}};
    const auto bytes = encode_shader(shader);
    AssetBuildInput input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = "forge.shader.diligent";
    input.importer_revision = std::string(64, 'd');
    input.output_format = "forge.shader.dxbc";
    input.platform = "windows-x64";
    input.backend = "d3d12";
    input.profile = "fxc-5.1";
    DerivedDataCache(root).publish(input, {{"program.shader", bytes}}, [](const auto& a) {
        (void)decode_shader(a.files.front().bytes);
    });
    const auto id = AssetId::generate();
    catalog.add({id,
                 "shader",
                 "surface.shader.json",
                 1,
                 {},
                 {{"forge.import",
                   {{"version", 1}, {"generation", std::uint64_t(1)}, {"key", input.key()}}},
                  {"forge.shader",
                   {{"version", 1},
                    {"compiler_input_key", shader.build_key},
                    {"layout", shader.layout_digest()}}}}});
    return id;
}
} // namespace forge::test
