#include "asset_bytes.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/shader_resource.hpp>
namespace forge {
namespace {
ResourceCandidate<ShaderAsset> candidate(std::span<const std::byte> bytes,
                                         const std::string& compiler_key, const std::string& layout,
                                         std::stop_token stop) {
    auto shader = std::make_unique<ShaderData>(decode_shader(bytes));
    if (shader->build_key != compiler_key || shader->layout_digest() != layout)
        throw std::runtime_error("Shader compiler/layout provenance differs from selection");
    if (stop.stop_requested())
        throw std::runtime_error("Shader resource load cancelled");
    const auto memory = shader->resident_bytes();
    return {std::move(shader), ResourceMemory{memory}};
}
} // namespace
ResourceTicket request_shader(ResourcePool<ShaderAsset>& pool, std::filesystem::path project,
                              const AssetCatalog& catalog, AssetRef<ShaderAsset> ref) {
    const auto found = catalog.records().find(ref.id);
    if (!ref.id || found == catalog.records().end() || found->second.type != ShaderAsset::type ||
        found->second.subasset || found->second.schema_version != 1)
        throw std::runtime_error("Shader reference is missing, incompatible or not a root asset");
    const auto& metadata = found->second.metadata;
    const auto& publication = metadata.at("forge.import");
    const auto& shader = metadata.at("forge.shader");
    const auto& generation = publication.at("generation");
    if (publication.at("version") != 1 || shader.at("version") != 1 ||
        !generation.is_number_integer() || generation.get<double>() <= 0)
        throw std::runtime_error("Invalid shader publication metadata");
    const auto key = publication.at("key").get<std::string>();
    const auto compiler_key = shader.at("compiler_input_key").get<std::string>();
    const auto layout = shader.at("layout").get<std::string>();
    resource_detail::valid_revision(key);
    resource_detail::valid_revision(compiler_key);
    resource_detail::valid_revision(layout);
    return pool.request(
        ref, key, generation.get<std::uint64_t>(),
        [project = std::move(project), key, compiler_key, layout](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Shader resource load cancelled");
            DerivedDataCache cache(project, {20 * 1024 * 1024, 32 * 1024 * 1024, 2});
            auto artifact = cache.load_selected(key, [](const CachedArtifact& a) {
                if (a.files.size() != 1 || a.files.front().name != "program.shader" ||
                    a.manifest.at("inputs").at("importer") != "forge.shader.diligent")
                    throw std::runtime_error("Invalid selected shader artifact");
            });
            return candidate(artifact.files.front().bytes, compiler_key, layout, stop);
        });
}
} // namespace forge
