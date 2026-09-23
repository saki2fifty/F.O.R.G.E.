#include "runtime_package.hpp"
#include "asset_bytes.hpp"
#include "audio_bundle.hpp"
#include "bounded_json.hpp"
#include "material_selection.hpp"
#include "model_render_resource.hpp"
#include "native_io_path.hpp"
#include "navigation_asset.hpp"
#include "publish_directory.hpp"
#include "runtime_document_package.hpp"
#include "texture_bundle_validation.hpp"
#include <forge/audio_components.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/scene.hpp>
#include <forge/shader_resource.hpp>
#include <fstream>
#include <set>
namespace forge {
namespace {
using Json = nlohmann::json;
using namespace asset_detail;
constexpr std::string_view manifest_name = "forge.runtime-content.json";
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void cancelled(std::stop_token stop) {
    require(!stop.stop_requested(), "Runtime content packaging cancelled");
}
void limits(const RuntimePackageLimits& value) {
    require(value.bytes && value.bytes <= 2ull * 1024 * 1024 * 1024 && value.assets &&
                value.assets <= 16384 && value.files && value.files <= 32768,
            "Invalid runtime package limits");
}
std::string platform(std::string name) {
    // Existing shader recipes name their x64 target explicitly; current desktop
    // texture/model/audio recipes use the OS name. No backend/profile aliasing.
    if (name == "windows-x64")
        return "windows";
    return name;
}
Json target_value(const RuntimePackageTarget& target) {
    auto word = [](const auto& text) {
        return !text.empty() && text.size() <= 64 &&
               std::all_of(text.begin(), text.end(), [](unsigned char c) {
                   return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
               });
    };
    require(word(target.platform) && word(target.backend), "Invalid runtime package target");
    return {{"platform", platform(target.platform)}, {"backend", target.backend}};
}
std::string revision(const AssetRecord& record) {
    if (package_detail::authored_document(record)) {
        const auto& metadata = record.metadata.at("forge.runtime_document");
        const auto key = metadata.at("sha256").get<std::string>();
        require(metadata.at("version") == 1 && valid_content_digest(key) &&
                    valid_content_digest(metadata.at("schema_digest").get<std::string>()),
                "Invalid authored runtime document revision");
        return key;
    }
    if (record.type == NavMeshAsset::type && record.schema_version == 1 && !record.subasset) {
        const auto key = record.metadata.at("sha256").get<std::string>();
        require(valid_content_digest(key), "Invalid navigation artifact revision");
        return key;
    }
    require(record.schema_version == 1 && record.metadata.contains("forge.import"),
            "Asset " + record.id.str() + " (" + record.type +
                ") has no supported cooked selection; import it before packaging");
    const auto& imported = record.metadata.at("forge.import");
    const auto key = imported.at("key").get<std::string>();
    require(imported.at("version") == 1 && valid_content_digest(key) &&
                imported.at("generation").is_number_unsigned() &&
                imported.at("generation").get<std::uint64_t>() > 0,
            "Invalid runtime asset selection");
    return key;
}
std::filesystem::path artifact_path(std::string_view key) {
    return std::filesystem::path(".forge/cache/derived") / key;
}
std::set<AssetId> closure(const AssetCatalog& catalog, std::span<const AssetId> roots,
                          RuntimePackageLimits budget) {
    require(!roots.empty() && roots.size() <= budget.assets, "Invalid runtime package roots");
    std::set<AssetId> selected;
    std::vector<AssetId> pending(roots.begin(), roots.end());
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        require(bool(id), "Empty runtime asset identity");
        if (!selected.insert(id).second)
            continue;
        require(selected.size() <= budget.assets, "Runtime asset closure exceeds limit");
        if (engine_asset(id))
            continue;
        const auto found = catalog.records().find(id);
        require(found != catalog.records().end(), "Missing runtime dependency: " + id.str());
        const auto& record = found->second;
        require(!record.subasset || !record.subasset->removed,
                "Removed runtime dependency: " + id.str());
        if (record.subasset)
            pending.push_back(record.subasset->owner);
        require(record.dependencies.empty() || !record.dependency_edges.empty(),
                "Runtime packaging requires typed dependencies for " + id.str());
        for (const auto& edge : catalog.dependency_graph().dependencies(id)) {
            if (edge.kind != AssetDependencyKind::Runtime)
                continue;
            const auto other = catalog.records().find(edge.target);
            const auto* engine = engine_asset(edge.target);
            require(engine ? edge.expected_type == engine->type
                           : other != catalog.records().end() &&
                                 other->second.type == edge.expected_type &&
                                 (!other->second.subasset || !other->second.subasset->removed),
                    "Missing/incompatible runtime dependency of " + id.str() + ": " +
                        edge.target.str());
            if (!engine && !edge.revision.empty())
                require(revision(other->second) == edge.revision,
                        "Runtime dependency revision disagrees with selected asset");
            pending.push_back(edge.target);
        }
    }
    return selected;
}
AssetRecord runtime_record(AssetRecord record) {
    const auto key = revision(record);
    const bool navigation = record.type == NavMeshAsset::type;
    const bool document = package_detail::authored_document(record);
    record.source =
        document     ? std::filesystem::path("runtime") / record.type / (record.id.str() + ".json")
        : navigation ? std::filesystem::path("runtime/navigation") / (record.id.str() + ".fnav")
                     : artifact_path(key) / "manifest.json";
    record.source_dependencies.clear();
    std::erase_if(record.dependency_edges,
                  [](const auto& edge) { return edge.kind != AssetDependencyKind::Runtime; });
    std::set<AssetId> targets;
    for (const auto& edge : record.dependency_edges)
        targets.insert(edge.target);
    record.dependencies.assign(targets.begin(), targets.end());
    // Native navigation's complete envelope provenance is admitted by its existing
    // loader. Its source scene is a build dependency, not a shipped source file.
    if (navigation)
        return record;
    if (document) {
        record.metadata = {
            {"forge.runtime_document", record.metadata.at("forge.runtime_document")}};
        return record;
    }
    Json metadata = Json::object();
    for (const auto* name :
         {"forge.import", "forge.model", "forge.material", "forge.shader", "forge.audio"})
        if (record.metadata.contains(name))
            metadata[name] = record.metadata.at(name);
    record.metadata = std::move(metadata);
    return record;
}
Json artifact_profile(const CachedArtifact& artifact, const AssetRecord& root,
                      const RuntimePackageTarget& target) {
    const auto& input = artifact.manifest.at("inputs");
    const auto& selected = root.metadata.at("forge.import");
    require(platform(input.at("platform")) == platform(target.platform) &&
                input.at("backend") == target.backend,
            "Cooked artifact target differs from requested package target: " + root.id.str());
    for (const auto* name : {"importer", "importer_revision", "output_format", "output_version",
                             "platform", "backend", "profile"})
        require(input.at(name) == selected.at(name),
                "Selected catalog recipe differs from immutable artifact");
    require(input.at("source") == selected.at("source_digest") &&
                selected.at("artifact_digest") == asset_build_digest(artifact.manifest.at("files")),
            "Selected artifact provenance/hash mismatch");
    return {{"platform", input.at("platform")},
            {"backend", input.at("backend")},
            {"profile", input.at("profile")},
            {"format", input.at("output_format")},
            {"version", input.at("output_version")}};
}
void admit_bundle(const AssetRecord& root, const CachedArtifact& artifact) {
    const auto format = root.metadata.at("forge.import").at("output_format");
    if (root.type == ModelAsset::type && format == "forge.model-bundle")
        (void)validate_model_bundle(artifact.files);
    else if (root.type == TextureAsset::type && format == "forge.texture-bundle")
        (void)validate_texture_bundle(artifact.files);
    else if (root.type == MaterialAsset::type && format == "forge.material-bundle")
        (void)decode_material_bundle(artifact.files, root.id);
    else if (root.type == AudioClipAsset::type && format == "forge.audio-clip") {
        const auto metadata = validate_audio_bundle(artifact.files);
        require(metadata == root.metadata.at("forge.audio") &&
                    metadata.at("source_digest") ==
                        root.metadata.at("forge.import").at("source_digest"),
                "Cooked AudioClip metadata differs from selection");
    } else if (root.type == ShaderAsset::type && format == "forge.shader.dxbc") {
        require(artifact.files.size() == 1 && artifact.files.front().name == "program.shader",
                "Unexpected compiled shader bundle");
        const auto shader = decode_shader(artifact.files.front().bytes);
        require(root.metadata.at("forge.shader").at("compiler_input_key") == shader.build_key &&
                    root.metadata.at("forge.shader").at("layout") == shader.layout_digest(),
                "Compiled shader/layout provenance differs from selection");
    } else
        throw std::runtime_error("No cooked runtime packaging adapter for " + root.type +
                                 "; asset " + root.id.str());
}
void validate_selections(const std::filesystem::path& root, const AssetCatalog& catalog,
                         std::stop_token stop) {
    for (const auto& [id, record] : catalog.records()) {
        cancelled(stop);
        if (record.subasset)
            continue; // Complete family admission checks all members and bindings.
        if (package_detail::authored_document(record)) {
            auto bytes = read_bytes(ProjectPaths(root).resolve(record.source), 64 * 1024 * 1024);
            (void)package_detail::admit_document(record, bytes);
            require(content_digest(bytes) == revision(record),
                    "Runtime document revision mismatch");
            if (record.type == SceneAsset::type)
                (void)load_game_scene(root, {id});
        } else if (record.type == NavMeshAsset::type)
            (void)navigation_detail::load(root, record);
        else if (record.type == ModelAsset::type)
            (void)load_model_selection(root, catalog, id, stop);
        else if (record.type == MaterialAsset::type) {
            const auto material = load_material_selection(root, catalog, {id}, stop);
            require(material.data.textures.size() == record.dependency_edges.size(),
                    "Material runtime dependency count differs from cooked bindings");
            for (const auto& [role, ref] : material.data.textures)
                require(std::any_of(record.dependency_edges.begin(), record.dependency_edges.end(),
                                    [&](const auto& edge) {
                                        return edge.target == ref.id &&
                                               edge.expected_type == TextureAsset::type &&
                                               edge.role == "material.texture:" + role;
                                    }),
                        "Material cooked texture binding is absent from runtime closure");
        } else if (record.type == TextureAsset::type) {
            ResourcePool<TextureAsset> pool;
            const auto ticket =
                request_texture(pool, root, std::make_shared<const AssetCatalog>(catalog), {id});
            require(pool.wait(ticket, std::chrono::seconds(30)), ticket.inspect().diagnostic);
        } else if (record.type == ShaderAsset::type) {
            ResourcePool<ShaderAsset> pool;
            const auto ticket = request_shader(pool, root, catalog, {id});
            require(pool.wait(ticket, std::chrono::seconds(30)), ticket.inspect().diagnostic);
        }
    }
}
void ordinary(const std::filesystem::path& path) {
    // MSVC canonicalization may remove the extended-length Win32 prefix. Compare
    // equivalent OS spellings, without weakening the redirection check itself.
    require(native_io_path(std::filesystem::weakly_canonical(path)) == native_io_path(path) &&
                !std::filesystem::is_symlink(path),
            "Runtime package path must not redirect: " + path_utf8(path));
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::filesystem::create_directories(path.parent_path());
    ordinary(path);
    try {
        std::ofstream file(native_io_path(path), std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        file.flush();
    } catch (const std::ios_base::failure& e) {
        throw std::runtime_error("Cannot write runtime package file: " + path_utf8(path) + ": " +
                                 e.what());
    }
}
} // namespace
Json package_runtime_content(const std::filesystem::path& project,
                             const std::filesystem::path& destination,
                             std::span<const AssetId> roots, const RuntimePackageTarget& target,
                             RuntimePackageLimits budget, std::stop_token stop,
                             const Json& reference_schema) {
    limits(budget);
    const auto target_json = target_value(target);
    const auto root = std::filesystem::canonical(native_io_path(project));
    const auto before = read_bytes(AssetCatalog::project_index(root), max_asset_index_bytes);
    AssetCatalog catalog(root);
    catalog.restore(parse_bounded_json(before, max_asset_index_bytes, 4000000, 64));
    Json schema = reference_schema;
    if (schema.is_null()) {
        WorldContext world;
        Scene scene(world);
        schema = scene.schema();
    }
    package_detail::prepare_documents(catalog, root, roots, schema, budget, stop);
    const auto selected = closure(catalog, roots, budget);
    // Directory creation/enumeration need the same extended Windows spelling as
    // file streams. Keep it inside the package I/O owner; manifests remain relative.
    const auto final = native_io_path(std::filesystem::absolute(destination).lexically_normal());
    ordinary(final);
    require(!std::filesystem::exists(final) && std::filesystem::is_directory(final.parent_path()),
            "Package destination must be new, with an existing parent directory");
    const auto stage = final.parent_path() / (".forge-package-" + AssetId::generate().str());
    require(std::filesystem::create_directory(stage), "Cannot create package staging directory");
    std::map<std::filesystem::path, std::string> written;
    std::set<std::filesystem::path> directories{stage};
    auto stage_write = [&](const std::filesystem::path& locator, std::span<const std::byte> bytes) {
        const auto path = stage / locator;
        for (auto parent = path.parent_path(); parent != stage; parent = parent.parent_path())
            directories.insert(parent);
        write(path, bytes);
        written.emplace(path, content_digest(bytes));
    };
    try {
        Json manifest{{"format", "forge.runtime-content"},
                      {"version", 1},
                      {"target", target_json},
                      {"roots", std::set<AssetId>(roots.begin(), roots.end())},
                      {"artifacts", Json::object()},
                      {"files", Json::object()}};
        std::uint64_t total = 0;
        auto emit = [&](const auto& path, std::span<const std::byte> bytes) {
            cancelled(stop);
            require(bytes.size() <= budget.bytes - total &&
                        manifest["files"].size() + 1 < budget.files,
                    "Runtime package byte/file budget exceeded");
            total += bytes.size();
            const auto locator = ProjectPaths::normalize(path);
            require(!manifest["files"].contains(path_utf8(locator)), "Duplicate package file");
            stage_write(locator, bytes);
            manifest["files"][path_utf8(locator)] = {{"bytes", bytes.size()},
                                                     {"sha256", content_digest(bytes)}};
        };
        std::vector<AssetRecord> records;
        std::set<std::string> copied;
        for (const auto id : selected) {
            cancelled(stop);
            if (engine_asset(id))
                continue;
            const auto& record = catalog.records().at(id);
            records.push_back(runtime_record(record));
            if (record.subasset)
                continue;
            if (package_detail::authored_document(record)) {
                const auto bytes =
                    read_bytes(ProjectPaths(root).resolve(record.source), 64 * 1024 * 1024);
                (void)package_detail::admit_document(record, bytes);
                require(content_digest(bytes) == revision(record),
                        "Scene/prefab changed during export");
                emit(records.back().source, bytes);
                continue;
            }
            if (record.type == NavMeshAsset::type) {
                const auto bytes = read_bytes(ProjectPaths(root).resolve(record.source),
                                              navigation_detail::max_nav_bytes + 65556);
                const auto admitted = navigation_detail::admit(bytes);
                auto expected = record.metadata;
                expected.erase("sha256");
                require(admitted.metadata == expected &&
                            admitted.metadata.at("asset_id") == Json(record.id) &&
                            content_digest(bytes) == revision(record),
                        "Navigation selection differs from admitted immutable bytes");
                emit(records.back().source, bytes);
                continue;
            }
            const auto key = revision(record);
            auto artifact =
                DerivedDataCache(root, {256 * 1024 * 1024, 512 * 1024 * 1024, 4096})
                    .load_selected(key, [&](const auto& value) { admit_bundle(record, value); });
            manifest["artifacts"][key] = artifact_profile(artifact, record, target);
            if (!copied.insert(key).second)
                continue;
            for (const auto& file : artifact.files)
                emit(artifact_path(key) / file.name, file.bytes);
            const auto text = artifact.manifest.dump();
            emit(artifact_path(key) / "manifest.json", std::as_bytes(std::span(text)));
        }
        AssetCatalog packaged(stage);
        packaged.replace_all(std::move(records));
        const auto catalog_text = packaged.document().dump();
        emit("forge.assets.json", std::as_bytes(std::span(catalog_text)));
        const auto text = manifest.dump(2);
        require(text.size() <= 16 * 1024 * 1024 && text.size() <= budget.bytes - total,
                "Runtime package manifest exceeds budget");
        stage_write(manifest_name, std::as_bytes(std::span(text)));
        (void)open_runtime_content(stage, target, budget, stop);
        require(before == read_bytes(AssetCatalog::project_index(root), max_asset_index_bytes),
                "Asset selection changed during packaging; candidate discarded");
        cancelled(stop);
        ordinary(final);
        require(!std::filesystem::exists(final), "Package destination appeared during preparation");
        asset_detail::rename_new_directory(stage, final);
        return manifest;
    } catch (...) {
        std::error_code ignored;
        // Preserve unknown/externally modified staging content for inspection.
        for (const auto& [path, digest] : written) {
            try {
                ordinary(path);
                if (content_digest(read_bytes(path, 256 * 1024 * 1024)) == digest)
                    std::filesystem::remove(path, ignored);
            } catch (const std::exception&) {
            }
        }
        for (auto i = directories.rbegin(); i != directories.rend(); ++i)
            std::filesystem::remove(*i, ignored);
        throw;
    }
}
AssetCatalog open_runtime_content(const std::filesystem::path& package,
                                  const RuntimePackageTarget& target, RuntimePackageLimits budget,
                                  std::stop_token stop) {
    limits(budget);
    const auto root = native_io_path(std::filesystem::absolute(package).lexically_normal());
    ordinary(root);
    const auto manifest_path = root / manifest_name;
    ordinary(manifest_path);
    const auto manifest_bytes = read_bytes(manifest_path, 16 * 1024 * 1024);
    const auto manifest = parse_bounded_json(manifest_bytes, 16 * 1024 * 1024, 1000000, 32);
    require(manifest.at("format") == "forge.runtime-content" && manifest.at("version") == 1 &&
                manifest.at("target") == target_value(target) && manifest.at("files").is_object() &&
                manifest.at("files").size() < budget.files,
            "Runtime content format/target/file count mismatch");
    std::uint64_t total = manifest_bytes.size();
    require(total <= budget.bytes, "Runtime content exceeds byte budget");
    std::set<std::filesystem::path> files{std::filesystem::path(manifest_name)}, directories;
    for (const auto& [name, info] : manifest.at("files").items()) {
        cancelled(stop);
        const auto path = ProjectPaths::normalize(std::filesystem::u8path(name));
        require(path_utf8(path) == name && files.insert(path).second,
                "Invalid/duplicate runtime package path");
        ordinary(root / path);
        require(info.at("bytes").is_number_unsigned(), "Invalid package file byte count");
        const auto size = info.at("bytes").get<std::uint64_t>();
        require(size <= 256 * 1024 * 1024 && size <= budget.bytes - total,
                "Runtime package file exceeds budget");
        const auto bytes = read_bytes(root / path, std::size_t(size));
        require(bytes.size() == size && info.at("sha256") == content_digest(bytes),
                "Runtime package file/hash mismatch: " + name);
        total += size;
        for (auto parent = path.parent_path(); !parent.empty(); parent = parent.parent_path())
            directories.insert(parent);
    }
    // Reject unlisted authoring/debug files rather than accidentally shipping them.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        ordinary(entry.path());
        const auto path = entry.path().lexically_relative(root);
        require(entry.is_directory() ? directories.contains(path)
                                     : entry.is_regular_file() && files.contains(path),
                "Unmanifested runtime content: " + path_utf8(path));
    }
    require(files.contains("forge.assets.json"), "Runtime catalog is not in the package manifest");
    // The manifest requires a catalog. Never interpret a failed existence query
    // as an empty project in this admitted package path.
    AssetCatalog catalog(root);
    catalog.load(root / "forge.assets.json");
    const auto roots = manifest.at("roots").get<std::vector<AssetId>>();
    const auto selected = closure(catalog, roots, budget);
    std::size_t expected_records = 0;
    Json profiles = Json::object();
    std::set<std::filesystem::path> required_files{std::filesystem::path(manifest_name),
                                                   "forge.assets.json"};
    for (const auto id : selected) {
        cancelled(stop);
        if (engine_asset(id))
            continue;
        ++expected_records;
        const auto& record = catalog.records().at(id);
        const auto trimmed = runtime_record(record);
        require(record.source == trimmed.source && record.source_dependencies.empty() &&
                    record.dependency_edges == trimmed.dependency_edges &&
                    record.metadata == trimmed.metadata,
                "Runtime catalog contains authoring-only metadata or source locators");
        if (record.subasset)
            continue;
        if (record.type == NavMeshAsset::type || package_detail::authored_document(record)) {
            require(files.contains(record.source),
                    "Selected runtime document/data file not packaged");
            required_files.insert(record.source);
            continue; // Existing document/envelope admission validates bytes below.
        }
        const auto key = revision(record);
        const auto artifact =
            DerivedDataCache(root, {256 * 1024 * 1024, 512 * 1024 * 1024, 4096})
                .load_selected(key, [&](const auto& value) { admit_bundle(record, value); });
        profiles[key] = artifact_profile(artifact, record, target);
        required_files.insert(artifact_path(key) / "manifest.json");
        require(files.contains(artifact_path(key) / "manifest.json"),
                "Selected artifact manifest not packaged");
        for (const auto& file : artifact.files) {
            required_files.insert(artifact_path(key) / file.name);
            require(files.contains(artifact_path(key) / file.name), "Selected file not packaged");
        }
    }
    require(expected_records == catalog.records().size() && profiles == manifest.at("artifacts"),
            "Runtime catalog/artifact closure differs from declared roots");
    require(files == required_files, "Runtime package contains files outside its cooked closure");
    validate_selections(root, catalog, stop);
    return catalog;
}
} // namespace forge
