#pragma once
#include "asset_bytes.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/shader_asset.hpp>
namespace forge::test {
// Opaque CPU transport fixture. These marker bytes are NEVER submitted to a
// graphics API; native fixtures compile actual source through Diligent instead.
inline ShaderData publish_surface_fixture(const std::filesystem::path& root, AssetCatalog& catalog,
                                          AssetId id, const SurfaceShaderDefinition& definition,
                                          unsigned generation = 1) {
    AssetBuildInput input;
    const auto source = nlohmann::json{
        {"surface", surface_definition_document(definition)},
        {"generation",
         generation}}.dump();
    input.source_digest = asset_detail::content_digest(std::as_bytes(std::span(source)));
    input.importer = "forge.shader.diligent";
    input.importer_revision = std::string(64, 'b');
    input.output_format = "forge.shader.dxbc";
    input.platform = "windows-x64";
    input.backend = "d3d12";
    input.profile = "fxc-5.1";
    ShaderData data;
    data.build_key = input.key();
    data.compiler_digest = std::string(64, 'c');
    data.surface = definition;
    const nlohmann::json reflection{
        {"stage", "pixel"}, {"threads", {0, 0, 0}}, {"resources", nlohmann::json::array()}};
    data.stages = {{ShaderStage::Pixel,
                    "ForgeSurfaceColor",
                    {std::byte{1}},
                    reflection,
                    ShaderEntryRole::SurfaceColor},
                   {ShaderStage::Pixel,
                    "ForgeSurfaceDepth",
                    {std::byte{2}},
                    reflection,
                    ShaderEntryRole::SurfaceDepth}};
    const auto artifact = DerivedDataCache(root).publish(
        input, {{"program.shader", encode_shader(data)}},
        [](const auto& a) { (void)decode_shader(a.files.at(0).bytes); });
    AssetRecord record{id, "shader", "Assets/surface.shader.json"};
    record.metadata = {{"forge.import",
                        {{"version", 1},
                         {"generation", std::uint64_t(generation)},
                         {"key", input.key()},
                         {"source_digest", input.source_digest},
                         {"importer", input.importer},
                         {"importer_revision", input.importer_revision},
                         {"output_format", input.output_format},
                         {"output_version", input.output_version},
                         {"platform", input.platform},
                         {"backend", input.backend},
                         {"profile", input.profile},
                         {"artifact_digest", asset_build_digest(artifact.manifest.at("files"))}}},
                       {"forge.shader",
                        {{"version", 1},
                         {"layout", data.layout_digest()},
                         {"compiler_input_key", data.build_key}}}};
    if (catalog.records().contains(id))
        catalog.replace(std::move(record));
    else
        catalog.add(std::move(record));
    catalog.save(AssetCatalog::project_index(root));
    return data;
}
} // namespace forge::test
