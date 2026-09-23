#include "asset_bytes.hpp"
#include "audio_bundle.hpp"
#include "engine_render_resource.hpp"
#include "material_selection.hpp"
#include "model_render_resource.hpp"
#include "native_io_path.hpp"
#include "runtime_package.hpp"
#include "texture_bundle_validation.hpp"
#include <forge/audio_components.hpp>
#include <forge/game_content.hpp>
#include <forge/render_components.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F&& call, std::source_location where = std::source_location::current()) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Package rejection missing at " + std::to_string(where.line()));
}
void text(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(native_io_path(path), std::ios::binary | std::ios::trunc);
    check(bool(out.write(bytes.data(), std::streamsize(bytes.size()))) && bool(out.flush()),
          "Fixture write failed");
}
AssetRecord publish(const std::filesystem::path& project, AssetId id, const char* type,
                    const char* importer, const char* format, std::vector<ArtifactFile> files,
                    const DerivedDataCache::Validator& validate) {
    AssetBuildInput input;
    input.source_digest = std::string(64, 'a');
    input.importer = importer;
    input.importer_revision = std::string(64, 'b');
    input.output_format = format;
    input.platform = "linux";
    input.backend = "none";
    input.profile = "cpu";
    const auto artifact = DerivedDataCache(project).publish(input, std::move(files), validate);
    AssetRecord record{id, type, std::string(type) + ".authoring", 1, {}};
    record.metadata = {{"private.authoring-notes", std::string(16000, 'x')},
                       {"forge.import",
                        {{"version", 1},
                         {"generation", 1u},
                         {"key", artifact.key},
                         {"source_digest", input.source_digest},
                         {"importer", input.importer},
                         {"importer_revision", input.importer_revision},
                         {"output_format", format},
                         {"output_version", 1},
                         {"platform", input.platform},
                         {"backend", input.backend},
                         {"profile", input.profile},
                         {"artifact_digest", asset_build_digest(artifact.manifest.at("files"))}}}};
    record.source_dependencies = {{record.source, "fixture", input.source_digest}};
    text(project / record.source, "Raw development source is not needed by the selected revision.");
    return record;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Package fixture requires scratch directory");
        const auto scratch = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto project = scratch / "authoring";
        std::filesystem::create_directories(project);
        AssetCatalog catalog(project);
        const auto audio_id = AssetId::generate(), texture_id = AssetId::generate(),
                   material_id = AssetId::generate(), unrelated_id = AssetId::generate();
        const AudioClipData clip{{.25f, -.25f, 0, 1}, 4, 1, 48000};
        const auto audio_metadata = audio_clip_metadata(clip, std::string(64, 'a'));
        const auto audio_text = audio_metadata.dump();
        const auto audio_span = std::as_bytes(std::span(audio_text));
        auto audio =
            publish(project, audio_id, AudioClipAsset::type, "forge.audio.wav", "forge.audio-clip",
                    {{"audio.fpcm", encode_audio_clip(clip)},
                     {"audio.json", {audio_span.begin(), audio_span.end()}}},
                    [](const auto& a) { (void)validate_audio_bundle(a.files); });
        audio.metadata["forge.audio"] = audio_metadata;
        catalog.add(audio);
        TextureData texture;
        texture.width = texture.height = 1;
        texture.format = TextureFormat::RGBA8Srgb;
        texture.semantic = TextureSemantic::Color;
        texture.subresources = {std::vector<std::byte>(4, std::byte{255})};
        const auto texture_bytes = encode_texture(texture);
        TextureBundleIndex index;
        index.variants = {{TextureSemantic::Color, texture_variant_file(TextureSemantic::Color),
                           content_digest(texture_bytes), texture_bytes.size()}};
        auto texture_record = publish(
            project, texture_id, TextureAsset::type, "forge.texture.image", "forge.texture-bundle",
            {{"texture.json", encode_texture_bundle_index(index)},
             {index.variants.front().file, texture_bytes}},
            [](const auto& a) { (void)validate_texture_bundle(a.files); });
        catalog.add(texture_record);
        auto material = engine_material_resource(engine_material());
        material.values.textures["baseColorTexture"] = {TextureSemantic::Color};
        material.textures["baseColorTexture"] = {texture_id};
        auto material_record =
            publish(project, material_id, MaterialAsset::type, "forge.material.builtin",
                    "forge.material-bundle", encode_material_bundle(material_id, material),
                    [&](const auto& a) { (void)decode_material_bundle(a.files, material_id); });
        material_record.metadata["forge.material"] = {{"version", 1},
                                                      {"textures", material.textures}};
        material_record.dependency_edges = {
            {texture_id, TextureAsset::type, AssetDependencyKind::Runtime,
             "material.texture:baseColorTexture",
             texture_record.metadata.at("forge.import").at("key")},
            {unrelated_id, MaterialAsset::type, AssetDependencyKind::Build, "parent", {}}};
        material_record.dependencies = {texture_id, unrelated_id};
        catalog.add(material_record);
        catalog.add({unrelated_id, MaterialAsset::type, "unused.material.json", 1, {}});
        catalog.save(AssetCatalog::project_index(project));
        const auto original_index =
            read_bytes(AssetCatalog::project_index(project), max_asset_index_bytes);
        const std::array roots{material_id, audio_id, engine_primitive(0).id};
        const RuntimePackageTarget target{"linux", "none"};
        const auto first = scratch / "content one";
        const auto manifest = package_runtime_content(project, first, roots, target);
        const auto second = scratch / "content two";
        check(manifest == package_runtime_content(project, second, roots, target),
              "Package bytes depend on output path/time");
        // Staging and the 64-character artifact key can exceed MAX_PATH even
        // when the final destination fits. Exercise genuinely long final paths
        // too, without shortening the recipe key or persistent identities.
        auto deep = scratch;
        while (deep.native().size() < 290)
            deep /= "long-content-folder";
        std::filesystem::create_directories(native_io_path(deep));
        const auto long_package = deep / "content";
        check(manifest == package_runtime_content(project, long_package, roots, target) &&
                  open_runtime_content(long_package, target).records().size() == 3,
              "Long-path package changed content or could not load");
        const auto long_catalog = AssetCatalog::open_project(long_package);
        check(
            long_catalog.records().size() == 3 &&
                long_catalog.resolve(material_id, MaterialAsset::type).state ==
                    AssetState::Available &&
                !ProjectPaths(long_package).file_identity("forge.assets.json").empty() &&
                load_material_selection(long_package, long_catalog, {material_id}).data.textures ==
                    material.textures,
            "Long-path catalog existence, identity or resource lookup failed");
        check(read_bytes(AssetCatalog::project_index(project), max_asset_index_bytes) ==
                  original_index,
              "Packaging mutated project catalog");
        check(!std::filesystem::exists(first / ".forge/cache/derived/cache.lock"),
              "Runtime package created writer lock");
        check(!std::filesystem::exists(first / audio.source), "Package copied raw source");
        std::filesystem::remove_all(project);
        const auto relocated = scratch / "relocated content";
        std::filesystem::rename(first, relocated);
        auto loaded = open_runtime_content(relocated, target);
        check(loaded.records().size() == 3 && !loaded.records().contains(unrelated_id),
              "Wrong runtime dependency closure");
        for (const auto& [id, record] : loaded.records()) {
            check(record.source_dependencies.empty() &&
                      !record.metadata.contains("private.authoring-notes"),
                  "Authoring metadata leaked");
            check(loaded.resolve(id, record.type).state == AssetState::Available,
                  "Relocated logical lookup failed");
        }
        check(load_material_selection(relocated, loaded, {material_id}).data.textures ==
                  material.textures,
              "Relocated material binding changed");
        const auto audio_key =
            loaded.records().at(audio_id).metadata.at("forge.import").at("key").get<std::string>();
        const auto audio_path =
            std::filesystem::path(".forge/cache/derived") / audio_key / "audio.fpcm";
        check(decode_audio_clip(read_bytes(relocated / audio_path, max_audio_pcm_bytes)).pcm ==
                  clip.pcm,
              "Relocated PCM differs");
        std::filesystem::permissions(relocated / ".forge/cache/derived",
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_exec);
        (void)open_runtime_content(relocated, target);
        std::filesystem::permissions(relocated / ".forge/cache/derived",
                                     std::filesystem::perms::owner_all);
        rejects([&] { open_runtime_content(relocated, {"linux", "vulkan"}); });
        rejects([&] { open_runtime_content(relocated, target, {16, 1, 1}); });
        const auto manifest_path = relocated / "forge.runtime-content.json";
        auto malformed = manifest;
        malformed["files"]["../escape"] = {{"bytes", 0u}, {"sha256", std::string(64, 'a')}};
        text(manifest_path, malformed.dump());
        rejects([&] { open_runtime_content(relocated, target); });
        malformed = manifest;
        malformed["artifacts"][audio_key]["profile"] = "another-profile";
        text(manifest_path, malformed.dump());
        rejects([&] { open_runtime_content(relocated, target); });
        text(manifest_path, manifest.dump(2));
        text(relocated / "debug.txt", "must not be shipped");
        rejects([&] { open_runtime_content(relocated, target); });
        std::filesystem::remove(relocated / "debug.txt");
        const auto good_pcm = read_bytes(relocated / audio_path, max_audio_pcm_bytes);
        text(relocated / audio_path, "truncated");
        rejects([&] { open_runtime_content(relocated, target); });
        text(relocated / audio_path,
             std::string_view(reinterpret_cast<const char*>(good_pcm.data()), good_pcm.size()));
        (void)open_runtime_content(relocated, target);
        const auto protected_manifest = read_bytes(manifest_path, 16 * 1024 * 1024);
        rejects([&] { package_runtime_content(second, relocated, roots, target); });
        check(read_bytes(manifest_path, 16 * 1024 * 1024) == protected_manifest,
              "Failed replacement damaged previous package");
        const auto absent = scratch / "rejected";
        rejects([&] { package_runtime_content(second, absent, roots, {"windows", "d3d12"}); });
        check(!std::filesystem::exists(absent), "Wrong-target package published");
        rejects([&] { package_runtime_content(second, absent, roots, target, {100, 4, 10}); });
        std::stop_source stop;
        stop.request_stop();
        rejects(
            [&] { package_runtime_content(second, absent, roots, target, {}, stop.get_token()); });
        auto bad_catalog = AssetCatalog::open_project(second).document();
        for (auto& row : bad_catalog["assets"])
            if (row.at("id") == material_id.str())
                row["dependency_edges"][0]["type"] = "audio_clip";
        text(AssetCatalog::project_index(second), bad_catalog.dump());
        rejects([&] { package_runtime_content(second, absent, roots, target); });
        check(!std::filesystem::exists(absent), "Invalid closure published");
        for (const auto& entry : std::filesystem::directory_iterator(scratch))
            check(!entry.path().filename().string().starts_with(".forge-package-"),
                  "Failed candidate staging leaked");
        const auto authored = scratch / "scene-source";
        std::filesystem::create_directory(authored);
        WorldContext world;
        Scene scene(world);
        const auto entity = EntityId::generate();
        auto document = empty_scene();
        document["entities"] =
            Json::array({{{"id", entity}, {"name", "Cube"}, {"components", Json::object()}}});
        scene.replace(document);
        scene.entity(entity.str()).set<MeshRenderer>({engine_primitive(0), {}});
        document = scene.document();
        scene.save(authored / "level.scene.json");
        AssetCatalog scene_catalog(authored);
        const auto scene_record = scene_catalog.add_scene("level.scene.json");
        scene_catalog.save(AssetCatalog::project_index(authored));
        const auto scene_index =
            read_bytes(AssetCatalog::project_index(authored), max_asset_index_bytes);
        const std::array scene_roots{scene_record.id};
        const auto scene_output = scratch / "scene-output";
        (void)package_runtime_content(authored, scene_output, scene_roots, target);
        check(read_bytes(AssetCatalog::project_index(authored), max_asset_index_bytes) ==
                  scene_index,
              "Export changed source dependency catalog");
        const auto packaged_catalog = open_runtime_content(scene_output, target);
        check(packaged_catalog.dependency_graph().dependencies(scene_record.id).size() == 1,
              "Scene's reflected mesh reference missing from authoritative graph");
        (void)package_runtime_content(scene_output, scratch / "scene-repacked", scene_roots,
                                      target);
        std::filesystem::rename(authored, scratch / "unavailable-project");
        scene.restore_snapshot(load_game_scene(scene_output, {scene_record.id}));
        check(scene.entity(entity.str()).is_alive(), "Relocated packaged scene did not load");
        std::filesystem::rename(scratch / "unavailable-project", authored);
        auto missing = document;
        missing["asset_id"] = scene_record.id;
        missing["entities"][0]["components"]["forge.mesh_renderer"]["mesh"] = AssetId::generate();
        text(authored / "level.scene.json", missing.dump());
        rejects([&] {
            package_runtime_content(authored, scratch / "missing-mesh", scene_roots, target);
        });
        check(!std::filesystem::exists(scratch / "missing-mesh"),
              "Missing typed mesh was silently omitted");
        missing["entities"][0]["components"].erase("forge.mesh_renderer");
        missing["entities"][0]["components"]["plugin.unknown"] = {{"asset", AssetId::generate()}};
        text(authored / "level.scene.json", missing.dump());
        rejects(
            [&] { package_runtime_content(authored, scratch / "opaque", scene_roots, target); });
        const auto prefab_id = AssetId::generate();
        const auto member_id = PrefabMemberId::generate();
        Json prefab{{"format", "forge.prefab"},
                    {"version", 2},
                    {"asset_id", prefab_id},
                    {"revision", 1u},
                    {"root", member_id},
                    {"members",
                     Json::array({{{"id", member_id},
                                   {"name", "Part"},
                                   {"components", document.at("entities")[0].at("components")}}})}};
        text(authored / "part.prefab.json", prefab.dump());
        scene_catalog.add({prefab_id, PrefabAsset::type, "part.prefab.json", 2});
        scene_catalog.save(AssetCatalog::project_index(authored));
        auto instance = document;
        instance["version"] = 5;
        instance["entities"][0]["components"] = Json::object();
        instance["entities"][0]["prefab_instance"] = {
            {"asset", prefab_id}, {"revision", 1u}, {"members", {{member_id.str(), entity}}}};
        text(authored / "level.scene.json", instance.dump());
        const auto prefab_output = scratch / "prefab-output";
        (void)package_runtime_content(authored, prefab_output, scene_roots, target);
        auto loaded_prefab = load_game_scene(prefab_output, {scene_record.id});
        check(loaded_prefab.at("_prefab_sources").size() == 1 &&
                  loaded_prefab.at("entities")[0].at("components").empty(),
              "Prefab closure changed authored inheritance");
        scene.restore_snapshot(loaded_prefab);
        check(!scene.entity(entity.str()).owns<MeshRenderer>() &&
                  scene.entity(entity.str()).get<MeshRenderer>().mesh == engine_primitive(0),
              "Relocated prefab lost inherited mesh binding");
        std::filesystem::rename(authored / "part.prefab.json",
                                authored / "unavailable.prefab.json");
        rejects([&] {
            package_runtime_content(authored, scratch / "missing-prefab", scene_roots, target);
        });
        std::filesystem::remove_all(native_io_path(scratch));
        std::cout << "Runtime package closure, relocation, source independence, limits, hashes and "
                     "failure preservation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
