#include "asset_file_operations.hpp"
#include "asset_file_service_tests.hpp"
#include "asset_storage.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include <forge/asset_publication.hpp>
#include <forge/material_source.hpp>
#include <forge/scene_identity.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid source operation accepted");
}
void write(const std::filesystem::path& p, const nlohmann::json& j) {
    asset_storage::replace(p, j.dump(2));
}
nlohmann::json read(const std::filesystem::path& p) {
    return nlohmann::json::parse(*asset_storage::read(p));
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected test project");
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root / "Assets");
        // CTest uses its own fixture directory; retain previous runs separately.
        const auto project = root / AssetId::generate().str();
        std::filesystem::create_directories(project / "Assets");
        std::filesystem::create_directories(project / "Moved");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{project};
        auto lease = std::make_shared<ProjectLease>(project);
        AssetFileTransaction transaction(*lease);
        AssetCatalog catalog(project);
        const auto scene_id = AssetId::generate();
        const auto a = EntityId::generate(), b = EntityId::generate();
        nlohmann::json scene = {
            {"version", 3},
            {"asset_id", scene_id},
            {"opaque", {{"scene", scene_id}, {"entity", a}}},
            {"entities",
             nlohmann::json::array(
                 {{{"id", a}, {"name", "Parent"}, {"components", nlohmann::json::object()}},
                  {{"id", b},
                   {"name", "Child"},
                   {"parent", a},
                   {"components", {{"opaque.plugin", {{"target", a}}}}}}})}};
        write(project / "Assets/source.scene.json", scene);
        catalog.add({scene_id, "scene", "Assets/source.scene.json", 3, {}});
        catalog.save(AssetCatalog::project_index(project));
        auto copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, scene_id, "Assets/copy.scene.json"},
            rewrite_authored_asset);
        check(copy.result != scene_id && copy.duplicated.at(scene_id) == copy.result,
              "Scene plan reused identity");
        transaction.commit(copy.changes, false);
        const auto duplicated = read(project / "Assets/copy.scene.json");
        check(duplicated.at("asset_id").get<AssetId>() == copy.result &&
                  duplicated.at("entities")[0].at("id") != scene.at("entities")[0].at("id") &&
                  duplicated.at("entities")[1].at("parent") ==
                      duplicated.at("entities")[0].at("id") &&
                  duplicated.at("opaque") == scene.at("opaque") &&
                  duplicated.at("entities")[1].at("components") ==
                      scene.at("entities")[1].at("components"),
              "Scene source duplicate failed identity/reference/opaque preservation");
        rejects([&] { duplicate_scene_asset(scene, scene_id); });
        rejects([&] { duplicate_scene_asset(scene, AssetId{}); });
        auto move = prepare_asset_file_operation(
            project, {AssetFileAction::Move, copy.result, "Moved/copy.scene.json"},
            rewrite_authored_asset);
        transaction.commit(move.changes, false);
        check(AssetCatalog::open_project(project).records().at(copy.result).source ==
                      "Moved/copy.scene.json" &&
                  read(project / "Moved/copy.scene.json").at("asset_id").get<AssetId>() ==
                      copy.result &&
                  !std::filesystem::exists(project / "Assets/copy.scene.json"),
              "Move changed scene identity");
        auto stale =
            prepare_asset_file_operation(project, {AssetFileAction::Delete, copy.result, {}});
        asset_storage::replace(project / "Moved/copy.scene.json", "external edit");
        rejects([&] { transaction.commit(stale.changes, true); });
        check(asset_storage::read(project / "Moved/copy.scene.json") == "external edit",
              "Stale deletion overwrote external bytes");
        write(project / "Moved/copy.scene.json", duplicated);
        auto deletion =
            prepare_asset_file_operation(project, {AssetFileAction::Delete, copy.result, {}});
        const auto receipt = transaction.commit(deletion.changes, true);
        check(!AssetCatalog::open_project(project).records().contains(copy.result) &&
                  !std::filesystem::exists(project / "Moved/copy.scene.json") &&
                  std::filesystem::exists(project / receipt.retained_files / "operation.json"),
              "Delete did not retain recovery bytes or remove selected identity");

        // Model-like family exercises the actual frozen identity duplicate service.
        const auto model = AssetId::generate(), member = AssetId::generate();
        const std::string raw = "raw external format bytes";
        asset_storage::replace(project / "Assets/model.fixture", raw);
        SubassetIdentityDocument identity{
            model,
            "Assets/model.fixture",
            std::string(64, 'a'),
            "fixture.v1",
            {{member, "member-key", "mesh", "Part", {}, false, {{"opaque", member}}}},
            nlohmann::json::object()};
        AssetImportSidecar sidecar;
        sidecar.settings.importer = "fixture.importer";
        sidecar.identity = identity;
        sidecar.build_inputs = nlohmann::json::object();
        write(project / "Assets/model.fixture.forge-import.json", sidecar.document());
        catalog = AssetCatalog::open_project(project);
        std::vector<AssetRecord> records;
        for (const auto& [id, r] : catalog.records()) {
            (void)id;
            records.push_back(r);
        }
        AssetRecord parent{
            model,
            "model",
            "Assets/model.fixture",
            1,
            {member},
            {{"opaque", {{"target", member}}}, {"forge.import", {{"key", std::string(64, 'b')}}}}};
        parent.dependency_edges = {
            {member, "mesh", AssetDependencyKind::Runtime, "part", std::string(64, 'b')}};
        records.push_back(parent);
        AssetRecord child{member, "mesh", "Assets/model.fixture", 1, {}};
        child.subasset = AssetSubasset{model, "member-key", false};
        records.push_back(child);
        catalog.replace_all(records);
        catalog.save(AssetCatalog::project_index(project));
        const auto raw_adapter = [](const auto&, std::string_view data, const auto&, const auto&) {
            return std::string(data); // This fixture format has no embedded identity/locator.
        };
        auto model_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, model, "Assets/model-copy.fixture"}, raw_adapter);
        transaction.commit(model_copy.changes, false);
        catalog = AssetCatalog::open_project(project);
        const auto& new_parent = catalog.records().at(model_copy.result);
        const auto new_child = model_copy.duplicated.at(member);
        check(new_child != member && new_parent.dependencies == std::vector<AssetId>{new_child} &&
                  new_parent.dependency_edges.front().target == new_child &&
                  new_parent.dependency_edges.front().revision.empty() &&
                  !new_parent.metadata.contains("forge.import") &&
                  new_parent.metadata.at("opaque") == parent.metadata.at("opaque") &&
                  catalog.records().at(new_child).subasset->owner == model_copy.result &&
                  catalog.records().at(new_child).subasset->key != "member-key",
              "Container duplicate reused selection, identities, keys or opaque remap");
        const auto copied_sidecar = AssetImportSidecar::parse(
            *asset_storage::read(project / "Assets/model-copy.fixture.forge-import.json"));
        check(copied_sidecar.identity.owner == model_copy.result &&
                  copied_sidecar.identity.entries.front().id == new_child &&
                  copied_sidecar.identity.entries.front().unknown ==
                      identity.entries.front().unknown,
              "Container sidecar identity/opaque preservation failed");
        rejects([&] {
            prepare_asset_file_operation(project, {AssetFileAction::Delete, new_child, {}});
        });
        rejects([&] {
            prepare_asset_file_operation(
                project, {AssetFileAction::Duplicate, model, "Assets/model-copy.fixture"},
                raw_adapter);
        });
        const auto rewrite = project_asset_file_rewriter(project);
        const auto material = MaterialSource::create(AssetId::generate());
        write(project / "Assets/source.material.json", material.document);
        AssetImportService imports(lease, material_import_registry(), {"linux", "none", "cpu"});
        const auto publish_material = [&](const char* path, AssetId id) {
            imports.submit(
                imports.prepare(path, {}, id),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_material_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            check(imports.wait_idle(std::chrono::seconds(10)),
                  "Material file fixture import stalled");
            const auto outcomes = imports.poll();
            check(outcomes.size() == 1 && outcomes[0].published,
                  "Material file fixture publication failed");
        };
        publish_material("Assets/source.material.json", material.asset());
        auto material_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, material.asset(), "Moved/copied.material.json"},
            rewrite);
        transaction.commit(material_copy.changes, false);
        check(read(project / "Moved/copied.material.json").at("asset_id").get<AssetId>() ==
                  material_copy.result,
              "Copied material retained its source-owned UUID");
        publish_material("Moved/copied.material.json", material_copy.result);
        catalog = AssetCatalog::open_project(project);
        const auto loaded =
            asset_detail::load_material_selection(project, catalog, {material_copy.result});
        check(loaded.asset == material_copy.result,
              "Copied material cannot load its own published identity");

        const auto shader = AssetId::generate();
        const nlohmann::json shader_source = {
            {"format", "forge.shader"},
            {"version", 1},
            {"asset_id", shader},
            {"source_root", "Assets"},
            {"stages", nlohmann::json::array({{{"stage", "vertex"}, {"source", "source.hlsl"}}})},
            {"opaque", {{"id", shader}}}};
        write(project / "Assets/source.shader.json", shader_source);
        catalog.add({shader, "shader", "Assets/source.shader.json", 1, {}});
        catalog.save(AssetCatalog::project_index(project));
        auto shader_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, shader, "Moved/copied.shader.json"}, rewrite);
        transaction.commit(shader_copy.changes, false);
        const auto shader_document = read(project / "Moved/copied.shader.json");
        check(shader_document.at("asset_id").get<AssetId>() == shader_copy.result &&
                  shader_document.at("source_root") == "Assets" &&
                  shader_document.at("opaque") == shader_source.at("opaque"),
              "Shader copy changed project-root source mapping or opaque data");

        const auto prefab = AssetId::generate();
        const auto prefab_member = PrefabMemberId::generate();
        const nlohmann::json prefab_source = {
            {"format", "forge.prefab"},
            {"version", 2},
            {"asset_id", prefab},
            {"revision", 3},
            {"root", prefab_member},
            {"members", nlohmann::json::array({{{"id", prefab_member},
                                                {"name", "Root"},
                                                {"components", nlohmann::json::object()}}})}};
        write(project / "Assets/source.prefab.json", prefab_source);
        catalog = AssetCatalog::open_project(project);
        catalog.add({prefab, "prefab", "Assets/source.prefab.json", 2, {}});
        catalog.save(AssetCatalog::project_index(project));
        auto prefab_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, prefab, "Moved/copied.prefab.json"}, rewrite);
        transaction.commit(prefab_copy.changes, false);
        const auto prefab_document = read(project / "Moved/copied.prefab.json");
        check(prefab_document.at("asset_id").get<AssetId>() == prefab_copy.result &&
                  prefab_document.at("root").get<PrefabMemberId>() != prefab_member &&
                  prefab_document.at("revision") == 1,
              "Prefab copy reused its member identity or revision");
        test_asset_file_service(project / "service");
        std::cout << "Asset file scene/family/move/delete/duplicate/publication tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
