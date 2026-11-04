#pragma once
#include "asset_bytes.hpp"
#include "shader_publication.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/shader_asset.hpp>
namespace forge::test {
// Opaque CPU transport fixture. These marker bytes are NEVER submitted to a
// graphics API; native fixtures compile actual source through Diligent instead.
inline ShaderData surface_fixture_program(const SurfaceShaderDefinition& definition,
                                          std::string build_key = std::string(64, 'a'),
                                          unsigned wrapper_version = 0) {
    ShaderData data;
    data.build_key = std::move(build_key);
    data.compiler_digest = std::string(64, 'c');
    data.surface = definition;
    data.surface_wrapper_version = wrapper_version           ? wrapper_version
                                   : definition.sprite_image ? 14
                                                             : 1;
    const nlohmann::json reflection{
        {"stage", "pixel"}, {"threads", {0, 0, 0}}, {"resources", nlohmann::json::array()}};
    unsigned marker = 1;
    for (auto role : surface_shader_roles(definition, data.surface_wrapper_version))
        data.stages.push_back({ShaderStage::Pixel,
                               surface_shader_entry(role),
                               {std::byte(marker++)},
                               reflection,
                               role});
    return data;
}
inline ShaderData publish_surface_fixture(const std::filesystem::path& root, AssetCatalog& catalog,
                                          AssetId id, const SurfaceShaderDefinition& definition,
                                          unsigned generation = 1, unsigned wrapper_version = 0) {
    AssetBuildInput input;
    auto source_document = nlohmann::json{{"surface", surface_definition_document(definition)},
                                          {"generation", generation}};
    if (wrapper_version)
        source_document["wrapper_version"] = wrapper_version;
    const auto source = source_document.dump();
    input.source_digest = asset_detail::content_digest(std::as_bytes(std::span(source)));
    input.importer = "forge.shader.diligent";
    input.importer_revision = std::string(64, 'b');
    input.output_format = "forge.shader.dxbc";
    input.platform = "windows-x64";
    input.backend = "d3d12";
    input.profile = "fxc-5.1";
    auto data = surface_fixture_program(definition, input.key(), wrapper_version);
    const auto artifact = DerivedDataCache(root).publish(
        input, {{"program.shader", encode_shader(data)}},
        [](const auto& a) { (void)decode_shader(a.files.at(0).bytes); });
    AssetRecord record;
    record.id = id;
    record.type = "shader";
    record.source = "Assets/surface.shader.json";
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
                       {"forge.shader", asset_detail::shader_publication_metadata(data)}};
    if (catalog.records().contains(id))
        catalog.replace(std::move(record));
    else
        catalog.add(std::move(record));
    catalog.save(AssetCatalog::project_index(root));
    return data;
}
} // namespace forge::test
