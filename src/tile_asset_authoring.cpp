#include "tile_asset_authoring.hpp"
#include "asset_bytes.hpp"
#include "cooked_envelope.hpp"
#include "sprite_draw_selection.hpp"
#include "tile_asset_resources.hpp"
#include "tile_asset_worker.hpp"
#include "tile_map_source.hpp"
#include "tile_set_source.hpp"
#include <thread>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Snapshot {
    std::vector<std::byte> bytes, definitions;
    AssetBuildInput input;
    Json data;
};
void dependency(AssetBuildInput& input, const AssetCatalog& catalog, AssetId id, const char* type,
                std::string role) {
    std::string revision;
    if (const auto* builtin = engine_asset(id)) {
        require(std::string_view(builtin->type) == type, "Tile engine dependency has wrong type");
        revision = engine_asset_revision(id);
    } else {
        const auto resolved = catalog.resolve(id, type);
        require(resolved.state == AssetState::Available &&
                    (std::string_view(type) != TileSetAsset::type || !resolved.record->subasset),
                "Tile dependency is missing, removed or has wrong type/ownership");
        const auto& selected = resolved.record->metadata.at("forge.import");
        require(selected.at("version") == 1, "Tile dependency has unsupported publication");
        revision = selected.at("key").get<std::string>();
    }
    require(valid_content_digest(revision), "Tile dependency has no immutable revision");
    input.dependencies.push_back(
        {id, type, AssetDependencyKind::Runtime, std::move(role), std::move(revision)});
}
void admit_images(const AssetImportRequest& request, std::shared_ptr<const AssetCatalog> catalog,
                  const TileSetValues& values, std::stop_token stop) {
    ResourcePool<MeshAsset> meshes({1, 1, 1, 64ull * 1024 * 1024});
    ResourcePool<MaterialAsset> materials({1, 16, 32, 64ull * 1024 * 1024});
    ResourcePool<TextureAsset> textures({1, 32, 32, 128ull * 1024 * 1024});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (const auto& image : values.images) {
        require(!stop.stop_requested(), "Tile Set image admission cancelled");
        asset_detail::ModelDrawCandidate candidate(request.project, catalog, 1, engine_primitive(8),
                                                   {{"surface", image.material}}, meshes);
        while (!candidate.ready()) {
            require(!stop.stop_requested(), "Tile Set image admission cancelled");
            meshes.pump();
            materials.pump();
            textures.pump();
            candidate.advance(1, meshes, materials, textures);
            if (candidate.state() != ResourceState::Loading && !candidate.ready())
                throw std::runtime_error("Tile Set image admission failed: " +
                                         candidate.diagnostic());
            require(std::chrono::steady_clock::now() < deadline,
                    "Tile Set image admission timed out");
            if (!candidate.ready())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto& texture = asset_detail::sprite_source_texture(*candidate.ready());
        (void)sprite_grid_region(texture.width, texture.height, image.grid, 0);
    }
}
Snapshot capture(const AssetImportRequest& request, bool map, std::stop_token stop) {
    require(!stop.stop_requested(), "Tile asset import cancelled");
    Snapshot snapshot;
    snapshot.bytes =
        asset_detail::read_bytes(ProjectPaths(request.project).resolve(request.source),
                                 map ? tile_map_source_byte_limit : tile_set_source_byte_limit);
    auto& input = snapshot.input;
    input.source_digest = asset_detail::content_digest(snapshot.bytes);
    input.importer = map ? tile_map_importer_id : tile_set_importer_id;
    input.importer_revision = map ? tile_map_recipe_revision() : tile_set_recipe_revision();
    input.output_format = map ? tile_map_cooked_format : tile_set_cooked_format;
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    auto catalog =
        std::make_shared<const AssetCatalog>(AssetCatalog::open_project(request.project));
    snapshot.data = {{"asset", request.asset}, {"map", map}};
    if (!map) {
        const auto source = TileSetSource::parse(snapshot.bytes);
        require(source.asset() == request.asset, "Tile Set source/catalog identity mismatch");
        for (const auto& material : source.materials())
            dependency(input, *catalog, material.id, MaterialAsset::type,
                       "tile_set.material." + material.id.str());
        admit_images(request, catalog, source.values(), stop);
        return snapshot;
    }
    const auto source = TileMapSource::parse(snapshot.bytes);
    require(source.asset() == request.asset, "Tile Map source/catalog identity mismatch");
    const auto set = source.document.tile_set();
    dependency(input, *catalog, set, TileSetAsset::type, "tile_map.tile_set");
    ResourcePool<TileSetAsset> definitions({1, 1, 1, 8ull * 1024 * 1024});
    const auto ticket = request_tile_set(definitions, request.project, catalog, {set});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!definitions.wait(ticket, std::chrono::milliseconds(10))) {
        require(!stop.stop_requested(), "Tile Map definition admission cancelled");
        require(!resource_detail::terminal(ticket.inspect().state),
                "Tile Map Tile Set could not be admitted");
        require(std::chrono::steady_clock::now() < deadline,
                "Tile Map Tile Set admission timed out");
    }
    const auto ready = definitions.acquire(ticket);
    require(bool(ready), "Tile Map has no complete Tile Set lease");
    source.document.for_each_chunk([&](auto, auto cells, auto) {
        require(!stop.stop_requested(), "Tile Map reference admission cancelled");
        for (const auto& cell : cells)
            if (cell.tile)
                (void)ready->tile(cell.tile);
    });
    const auto& record = catalog->records().at(set);
    DerivedDataCache cache(request.project, {tile_set_cooked_metadata_limit + 24,
                                             tile_set_cooked_metadata_limit + 65536 + 24, 1});
    (void)cache.load_selected(input.dependencies[0].revision, [&](const auto& artifact) {
        (void)admit_tile_set_artifact(artifact, record);
        snapshot.definitions = artifact.files[0].bytes;
    });
    require(!snapshot.definitions.empty(), "Tile Map has no immutable Tile Set snapshot");
    snapshot.data["tile_set_digest"] = asset_detail::content_digest(snapshot.definitions);
    return snapshot;
}
class TileImporter final : public AssetImporter {
    std::filesystem::path worker_;
    bool map_;

  public:
    TileImporter(std::filesystem::path worker, bool map)
        : AssetImporter(
              [&] {
                  AssetImporterDescriptor d;
                  d.id = map ? tile_map_importer_id : tile_set_importer_id;
                  d.revision = map ? tile_map_recipe_revision() : tile_set_recipe_revision();
                  d.label = map ? "Tile Map" : "Tile Set";
                  d.description = map ? "Validate sparse layers/cells against a published Tile Set "
                                        "and cook immutable runtime chunks."
                                      : "Validate published Sprite images, sheet grids and stable "
                                        "tile definitions.";
                  d.extensions = {".json"};
                  d.source_kinds = {map ? "forge.tile-map" : "forge.tile-set"};
                  d.output_types = {map ? TileMapAsset::type : TileSetAsset::type};
                  d.output_format = map ? tile_map_cooked_format : tile_set_cooked_format;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::IsolatedProcess;
                  const auto limits = asset_detail::tile_worker_limits();
                  d.limits = {limits.memory_bytes,
                              map ? tile_map_cooked_metadata_limit + tile_map_cooked_payload_limit +
                                        24
                                  : tile_set_cooked_metadata_limit + 24,
                              limits.seconds, 1};
                  return d;
              }(),
              {map ? tile_map_importer_id : tile_set_importer_id, 1, {}}),
          worker_(std::move(worker)), map_(map) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        auto name = path_utf8(source.source);
        for (auto& c : name)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        return name.ends_with(map_ ? ".tilemap.json" : ".tileset.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible,
                                       map_ ? "forge.tile-map" : "forge.tile-set",
                                       "FORGE authored tile asset source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported tile asset target");
        (void)settings().effective(request.settings);
        auto snapshot = capture(request, map_, stop);
        return {std::move(snapshot.input), {}, std::move(snapshot.data)};
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto current = capture(request, map_, stop);
        require(current.input.document() == plan.input.document() && current.data == plan.data,
                "Tile source or selected dependencies changed after discovery");
        Json payload{{"recipe", descriptor().id},
                     {"revision", descriptor().revision},
                     {"asset_id", request.asset},
                     {"source_digest", plan.input.source_digest}};
        if (map_)
            payload["tile_set_digest"] = current.data.at("tile_set_digest");
        asset_detail::ImportProcessRequest process{std::move(payload), {}};
        process.inputs.reserve(map_ ? 2 : 1);
        process.inputs.push_back({"source.bin", std::move(current.bytes)});
        if (map_)
            process.inputs.push_back({tile_set_artifact_file, std::move(current.definitions)});
        if (progress)
            progress(.25, "Cooking tile data in the bounded import worker");
        auto files = asset_detail::run_import_process(worker_, request.project, std::move(process),
                                                      asset_detail::tile_worker_limits(), stop);
        CachedArtifact candidate{{}, Json::object(), std::move(files)};
        validate(candidate);
        if (progress)
            progress(1., "Tile candidate validated");
        return std::move(candidate.files);
    }
    void validate(const CachedArtifact& artifact) const override {
        require(artifact.files.size() == 1 &&
                    artifact.files[0].name ==
                        (map_ ? tile_map_artifact_file : tile_set_artifact_file),
                "Tile candidate has wrong output file shape");
        // Identity is checked again against the publication owner, not invented
        // here. Worker recipe admission already bound the expected source UUID.
        const auto& bytes = artifact.files[0].bytes;
        const auto envelope = asset_detail::decode_envelope(
            bytes, map_ ? tile_map_cooked_magic : tile_set_cooked_magic,
            map_ ? tile_map_cooked_metadata_limit : tile_set_cooked_metadata_limit,
            map_ ? tile_map_cooked_payload_limit : 0);
        const auto id = envelope.metadata.at("asset_id").get<AssetId>();
        if (map_)
            (void)decode_cooked_tile_map(bytes, id);
        else
            (void)decode_cooked_tile_set(bytes, id);
    }
};
void publish(AssetPublicationCandidate& candidate, const AssetImportPlan& plan, bool map) {
    require(
        plan.input.importer == (map ? tile_map_importer_id : tile_set_importer_id) &&
            plan.input.importer_revision ==
                (map ? tile_map_recipe_revision() : tile_set_recipe_revision()) &&
            plan.input.output_format == (map ? tile_map_cooked_format : tile_set_cooked_format) &&
            plan.input.output_version == 1 && candidate.input.document() == plan.input.document() &&
            plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
            plan.data.at("map").get<bool>() == map && candidate.sidecar.identity.entries.empty() &&
            candidate.files.size() == 1 &&
            candidate.files[0].name == (map ? tile_map_artifact_file : tile_set_artifact_file),
        "Tile publication identity/recipe/file shape mismatch");
    const auto& file = candidate.files[0];
    std::map<std::string, std::pair<AssetId, std::string>> expected;
    if (map) {
        const auto data = decode_cooked_tile_map(file.bytes, candidate.ticket.owner);
        expected.emplace("tile_map.tile_set",
                         std::pair{data.tile_set(), std::string(TileSetAsset::type)});
    } else {
        const auto data = decode_cooked_tile_set(file.bytes, candidate.ticket.owner);
        for (const auto& material : data.materials())
            expected.emplace("tile_set.material." + material.id.str(),
                             std::pair{material.id, std::string(MaterialAsset::type)});
    }
    require(expected.size() == plan.input.dependencies.size(),
            "Tile publication dependency closure differs");
    for (const auto& edge : plan.input.dependencies) {
        const auto found = expected.find(edge.role);
        require(found != expected.end() && edge.target == found->second.first &&
                    edge.expected_type == found->second.second &&
                    edge.kind == AssetDependencyKind::Runtime &&
                    valid_content_digest(edge.revision),
                "Tile publication dependency role/type/identity mismatch");
        expected.erase(found);
    }
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = map ? "forge.tile-map.single.v1" : "forge.tile-set.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          map ? TileMapAsset::type : TileSetAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{map ? "forge.tile_map" : "forge.tile_set",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace
std::shared_ptr<const AssetImporterRegistry>
tile_asset_import_registry(std::filesystem::path worker) {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<TileImporter>(worker, false));
    registry->add(std::make_shared<TileImporter>(std::move(worker), true));
    registry->seal();
    return registry;
}
void prepare_tile_set_publication(AssetPublicationCandidate& candidate,
                                  const AssetImportPlan& plan) {
    publish(candidate, plan, false);
}
void prepare_tile_map_publication(AssetPublicationCandidate& candidate,
                                  const AssetImportPlan& plan) {
    publish(candidate, plan, true);
}
} // namespace forge
