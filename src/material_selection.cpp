#include "material_selection.hpp"
#include "bounded_json.hpp"
#include "pbr_material.hpp"
namespace forge::asset_detail {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
const AssetRecord& root_record(const AssetCatalog& catalog, AssetRef<MaterialAsset> ref) {
    const auto found = catalog.records().find(ref.id);
    require(bool(ref.id) && found != catalog.records().end(), "Material asset is missing");
    const auto& record = found->second;
    require(record.type == MaterialAsset::type && !record.subasset && record.schema_version == 1,
            "Material root has incompatible type/schema");
    const auto& imported = record.metadata.at("forge.import");
    require(record.metadata.at("forge.material").at("version").is_number_integer() &&
                record.metadata.at("forge.material").at("version") == 1,
            "Unsupported material publication metadata version");
    const auto& generation = imported.at("generation");
    require(imported.at("version") == 1 && imported.at("importer") == "forge.material.builtin" &&
                imported.at("output_format") == "forge.material-bundle" &&
                imported.at("output_version") == 1 && generation.is_number_unsigned() &&
                generation.get<std::uint64_t>() > 0 &&
                valid_content_digest(imported.at("key").get<std::string>()),
            "Invalid material publication selection");
    return record;
}
} // namespace
std::vector<ArtifactFile> encode_material_bundle(AssetId asset, const MaterialResourceData& data) {
    require(bool(asset), "Material bundle has no identity");
    validate_material_bindings(data.values, data.textures);
    (void)prepare_pbr_material(data.values);
    const auto text =
        nlohmann::json{{"version", 1}, {"asset", asset}, {"textures", data.textures}}.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {{"material.values", encode_material(data.values)},
            {"bindings.json", {bytes.begin(), bytes.end()}}};
}
MaterialResourceData decode_material_bundle(const std::vector<ArtifactFile>& files,
                                            AssetId expected) {
    require(files.size() == 2, "Material bundle file set mismatch");
    const ArtifactFile *values = nullptr, *bindings = nullptr;
    for (const auto& file : files) {
        if (file.name == "material.values" && !values)
            values = &file;
        else if (file.name == "bindings.json" && !bindings)
            bindings = &file;
        else
            throw std::runtime_error("Unexpected/duplicate material bundle file");
    }
    require(values && bindings, "Material bundle is incomplete");
    const auto j = parse_bounded_json(bindings->bytes, 65536, 4096, 8);
    require(j.at("version").is_number_integer() && j.at("version") == 1,
            "Unsupported material bindings version");
    const auto asset = j.at("asset").get<AssetId>();
    require(bool(asset) && (!expected || expected == asset), "Material bundle identity mismatch");
    require(j.at("textures").is_object() && j.at("textures").size() <= 64,
            "Material bundle texture count exceeds limit");
    MaterialResourceData result{decode_material(values->bytes),
                                j.at("textures").get<MaterialTextureBindings>()};
    validate_material_bindings(result.values, result.textures);
    (void)prepare_pbr_material(result.values);
    return result;
}
MaterialSelection load_material_selection(const std::filesystem::path& project,
                                          const AssetCatalog& catalog, AssetRef<MaterialAsset> ref,
                                          std::stop_token stop) {
    require(!stop.stop_requested(), "Material resource load cancelled");
    const auto& record = root_record(catalog, ref);
    const auto& metadata = record.metadata.at("forge.import");
    const auto key = metadata.at("key").get<std::string>();
    DerivedDataCache cache(project, {2 * 1024 * 1024, 3 * 1024 * 1024, 2});
    auto artifact = cache.load_selected(key, [&](const CachedArtifact& a) {
        (void)decode_material_bundle(a.files, ref.id);
        const auto& input = a.manifest.at("inputs");
        require(input.at("source") == metadata.at("source_digest") &&
                    input.at("importer") == metadata.at("importer") &&
                    input.at("importer_revision") == metadata.at("importer_revision") &&
                    input.at("output_format") == metadata.at("output_format") &&
                    input.at("output_version") == metadata.at("output_version") &&
                    asset_build_digest(a.manifest.at("files")) == metadata.at("artifact_digest"),
                "Material artifact and selected catalog revision disagree");
    });
    auto data = decode_material_bundle(artifact.files, ref.id);
    require(record.metadata.at("forge.material").at("textures") == nlohmann::json(data.textures),
            "Material catalog texture bindings differ from cooked revision");
    require(!stop.stop_requested(), "Material resource load cancelled");
    return {ref.id, key, metadata.at("generation").get<std::uint64_t>(), std::move(data)};
}
ResourceTicket request_root_material(ResourcePool<MaterialAsset>& pool,
                                     std::filesystem::path project,
                                     std::shared_ptr<const AssetCatalog> catalog,
                                     AssetRef<MaterialAsset> ref) {
    require(bool(catalog), "Material resource requires a catalog selection");
    const auto& metadata = root_record(*catalog, ref).metadata.at("forge.import");
    return pool.request(
        ref, metadata.at("key").get<std::string>(), metadata.at("generation").get<std::uint64_t>(),
        [project = std::move(project), catalog = std::move(catalog), ref](std::stop_token stop) {
            auto selected = load_material_selection(project, *catalog, ref, stop);
            auto value = std::make_unique<MaterialResourceData>(std::move(selected.data));
            const auto bytes = value->resident_bytes();
            return ResourceCandidate<MaterialAsset>{std::move(value), {bytes}};
        },
        {}, 0, "builtin:gltf-pbr-v1");
}
} // namespace forge::asset_detail
