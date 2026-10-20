#include "tile_asset_resources.hpp"
#include "asset_bytes.hpp"
#include <forge/engine_assets.hpp>
#include <optional>

namespace forge {
namespace {
using Json = nlohmann::json;
struct Contract {
    const char *type, *importer, *format, *metadata, *file;
    std::string recipe;
    std::size_t file_limit;
};
Contract set_contract() {
    return {TileSetAsset::type,
            tile_set_importer_id,
            tile_set_cooked_format,
            "forge.tile_set",
            tile_set_artifact_file,
            tile_set_recipe_revision(),
            tile_set_cooked_metadata_limit + 24};
}
Contract map_contract() {
    return {TileMapAsset::type,
            tile_map_importer_id,
            tile_map_cooked_format,
            "forge.tile_map",
            tile_map_artifact_file,
            tile_map_recipe_revision(),
            tile_map_cooked_metadata_limit + tile_map_cooked_payload_limit + 24};
}
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
const Json& selection(const AssetRecord& record, const Contract& contract) {
    require(record.type == contract.type && !record.subasset && bool(record.id),
            "Tile asset selection has wrong type, identity or ownership");
    const auto& selected = record.metadata.at("forge.import");
    const auto& generation = selected.at("generation");
    require(selected.at("version").is_number_integer() && selected.at("version") == 1 &&
                selected.at("importer") == contract.importer &&
                selected.at("importer_revision") == contract.recipe &&
                selected.at("output_format") == contract.format &&
                selected.at("output_version").is_number_integer() &&
                selected.at("output_version") == 1 &&
                (generation.is_number_unsigned() ||
                 (generation.is_number_integer() && generation.get<std::int64_t>() > 0)) &&
                generation.get<std::uint64_t>() != 0 &&
                valid_content_digest(selected.at("key").get<std::string>()),
            "Tile asset selection recipe/version/generation mismatch");
    return selected;
}
const ArtifactFile& file(const CachedArtifact& artifact, const AssetRecord& record,
                         const Contract& contract) {
    const Json& selected = selection(record, contract);
    require(artifact.key == selected.at("key").get_ref<const std::string&>() &&
                artifact.files.size() == 1 && artifact.files[0].name == contract.file &&
                artifact.files[0].bytes.size() <= contract.file_limit,
            "Tile asset selected artifact identity/files mismatch");
    const auto& result = artifact.files[0];
    const auto& input = artifact.manifest.at("inputs");
    for (const auto* name : {"importer", "importer_revision", "output_format", "output_version",
                             "platform", "backend", "profile"})
        require(input.at(name) == selected.at(name), "Tile asset artifact recipe mismatch");
    auto edges = record.dependency_edges;
    auto cooked = input.at("dependencies").get<std::vector<AssetDependency>>();
    std::sort(edges.begin(), edges.end());
    std::sort(cooked.begin(), cooked.end());
    const auto& metadata = record.metadata.at(contract.metadata);
    require(edges == cooked && input.at("source") == selected.at("source_digest") &&
                selected.at("artifact_digest") ==
                    asset_build_digest(artifact.manifest.at("files")) &&
                metadata.at("version").is_number_integer() && metadata.at("version") == 1 &&
                metadata.at("file") == result.name && metadata.at("bytes") == result.bytes.size() &&
                metadata.at("sha256") == asset_detail::content_digest(result.bytes),
            "Tile asset artifact/dependency/provenance mismatch");
    return result;
}
using ExpectedEdges = std::map<std::string, std::pair<AssetId, std::string>>;
void dependencies(const std::vector<AssetDependency>& edges, ExpectedEdges expected) {
    require(edges.size() == expected.size(), "Tile asset dependency closure is incomplete");
    for (const auto& edge : edges) {
        const auto found = expected.find(edge.role);
        require(found != expected.end() && edge.kind == AssetDependencyKind::Runtime &&
                    valid_content_digest(edge.revision) && edge.target == found->second.first &&
                    edge.expected_type == found->second.second,
                "Tile asset dependency role/type/identity mismatch");
        expected.erase(found);
    }
    require(expected.empty(), "Tile asset dependency roles are incomplete");
}
void current_dependencies(const AssetCatalog& catalog, const AssetRecord& record) {
    for (const auto& edge : record.dependency_edges) {
        std::string revision;
        if (const auto* builtin = engine_asset(edge.target)) {
            require(std::string_view(builtin->type) == edge.expected_type,
                    "Tile asset engine dependency has wrong type");
            revision = engine_asset_revision(edge.target);
        } else {
            const auto found = catalog.records().find(edge.target);
            require(found != catalog.records().end() && found->second.type == edge.expected_type &&
                        (!found->second.subasset || !found->second.subasset->removed) &&
                        (edge.expected_type != TileSetAsset::type || !found->second.subasset),
                    "Tile asset dependency is missing, removed or has wrong type/ownership");
            revision = found->second.metadata.at("forge.import").at("key").get<std::string>();
        }
        require(valid_content_digest(revision) && revision == edge.revision,
                "Tile asset dependency changed; recook against the selected revision");
    }
}
template <class Tag, class Admit>
ResourceTicket request(ResourcePool<Tag>& pool, const std::filesystem::path& project,
                       std::shared_ptr<const AssetCatalog> catalog, AssetRef<Tag> ref,
                       Contract contract, Admit admit) {
    require(bool(catalog), "Tile asset needs a catalog selection");
    // AssetCatalog::resolve also checks authoring source files. Runtime requests
    // use the immutable selected record, as the existing cooked resource owners
    // do, so offline sources and relocated packages remain loadable.
    const auto found = catalog->records().find(ref.id);
    require(found != catalog->records().end() && found->second.type == contract.type,
            "Tile asset is missing, removed or has wrong type");
    auto record = found->second;
    const Json& selected = selection(record, contract);
    current_dependencies(*catalog, record);
    const auto revision = selected.at("key").get<std::string>();
    const auto generation = selected.at("generation").get<std::uint64_t>();
    return pool.request(
        ref, revision, generation,
        [project, revision, contract = std::move(contract), record = std::move(record),
         admit](std::stop_token stop) {
            require(!stop.stop_requested(), "Tile asset load cancelled");
            DerivedDataCache cache(project, {contract.file_limit, contract.file_limit + 65536, 1});
            using Data = typename ResourceTraits<Tag>::Data;
            std::optional<Data> value;
            (void)cache.load_selected(
                revision, [&](const auto& artifact) { value = admit(artifact, record); });
            require(!stop.stop_requested() && value.has_value(),
                    "Tile asset load cancelled or empty");
            const auto bytes = value->resident_bytes();
            return ResourceCandidate<Tag>{std::make_unique<Data>(std::move(*value)),
                                          {0, 0, 0, 0, bytes}};
        });
}
} // namespace
TileSetData admit_tile_set_artifact(const CachedArtifact& artifact, const AssetRecord& record) {
    const auto& selected = file(artifact, record, set_contract());
    auto data = decode_cooked_tile_set(selected.bytes, record.id);
    ExpectedEdges expected;
    for (const auto& material : data.materials())
        expected.emplace("tile_set.material." + material.id.str(),
                         std::pair{material.id, std::string(MaterialAsset::type)});
    dependencies(record.dependency_edges, std::move(expected));
    return data;
}
TileMapData admit_tile_map_artifact(const CachedArtifact& artifact, const AssetRecord& record) {
    const auto& selected = file(artifact, record, map_contract());
    auto data = decode_cooked_tile_map(selected.bytes, record.id);
    dependencies(record.dependency_edges,
                 {{"tile_map.tile_set", {data.tile_set(), TileSetAsset::type}}});
    return data;
}
ResourceTicket request_tile_set(ResourcePool<TileSetAsset>& pool,
                                const std::filesystem::path& project,
                                std::shared_ptr<const AssetCatalog> catalog,
                                AssetRef<TileSetAsset> ref) {
    return request(pool, project, std::move(catalog), ref, set_contract(), admit_tile_set_artifact);
}
ResourceTicket request_tile_map(ResourcePool<TileMapAsset>& pool,
                                const std::filesystem::path& project,
                                std::shared_ptr<const AssetCatalog> catalog,
                                AssetRef<TileMapAsset> ref) {
    return request(pool, project, std::move(catalog), ref, map_contract(), admit_tile_map_artifact);
}
} // namespace forge
