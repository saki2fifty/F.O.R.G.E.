#include "animation_retarget.hpp"
#include "asset_file_operations.hpp"
#include "asset_file_service_tests.hpp"
#include "asset_reference_impact.hpp"
#include "asset_storage.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include <forge/animation_blend_space.hpp>
#include <forge/animation_graph.hpp>
#include <forge/animation_mask.hpp>
#include <forge/animation_states.hpp>
#include <forge/asset_publication.hpp>
#include <forge/material_graph.hpp>
#include <forge/material_source.hpp>
#include <forge/render_texture.hpp>
#include <forge/scene_identity.hpp>
#include <forge/transform_animation.hpp>
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
        auto graph = MaterialGraphSource::create(AssetId::generate());
        write(project / "Assets/graph.shader.json", graph.document);
        catalog = AssetCatalog::open_project(project);
        catalog.add({graph.asset(), "shader", "Assets/graph.shader.json"});
        catalog.save(AssetCatalog::project_index(project));
        auto graph_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, graph.asset(), "Moved/graph-copy.shader.json"},
            rewrite);
        transaction.commit(graph_copy.changes, false);
        const auto copied_graph =
            MaterialGraphSource{read(project / "Moved/graph-copy.shader.json")};
        check(copied_graph.asset() == graph_copy.result && copied_graph.asset() != graph.asset() &&
                  copied_graph.document.at("graph").at("nodes")[0].at("id") !=
                      graph.document.at("graph").at("nodes")[0].at("id"),
              "Shader file duplicate did not remap graph identities");
        (void)compile_material_graph(copied_graph.document.at("graph"));
        auto animation = TransformAnimationSource::create(AssetId::generate());
        auto slopes = TransformAnimationCurves{true};
        slopes.outgoing.translation = {2, 1, 0};
        slopes.incoming.scale = {.2, 0, 0};
        animation.set_key(0, {}, false, {}, slopes);
        animation.set_key(1000000, {{3, 1, 0}, {}, {2, 1, 1}}, false, {}, slopes);
        write(project / "Assets/Motion.animation.json", animation.document);
        catalog = AssetCatalog::open_project(project);
        catalog.add(
            {animation.asset(), "transform_animation", "Assets/Motion.animation.json", 1, {}});
        catalog.save(AssetCatalog::project_index(project));
        rejects([&] {
            prepare_asset_file_operation(
                project, {AssetFileAction::Duplicate, animation.asset(), "Moved/bad.json"},
                rewrite);
        });
        auto animation_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, animation.asset(), "Moved/Copy.animation.json"},
            rewrite);
        transaction.commit(animation_copy.changes, false);
        const auto copied_animation =
            TransformAnimationSource{read(project / "Moved/Copy.animation.json")};
        copied_animation.validate();
        check(copied_animation.asset() == animation_copy.result &&
                  copied_animation.asset() != animation.asset() &&
                  copied_animation.document.at("keys") == animation.document.at("keys"),
              "Animation duplicate did not isolate asset identity and preserve scoped keys");
        auto animation_move = prepare_asset_file_operation(
            project, {AssetFileAction::Move, animation_copy.result, "Assets/Copy.animation.json"},
            rewrite);
        transaction.commit(animation_move.changes, false);
        check(read(project / "Assets/Copy.animation.json") == copied_animation.document &&
                  !std::filesystem::exists(project / "Moved/Copy.animation.json"),
              "Animation move changed clip/key identities");
        auto states = AnimationStatesSource::create(AssetId::generate());
        const auto rig = AssetId::generate(), state_clip = AssetId::generate();
        states.document["skeleton"] = rig;
        states.document["entry"] = 5;
        states.document["next_id"] = 6;
        states.document["states"] = Json::array({{{"id", 5},
                                                  {"name", "Walk"},
                                                  {"clip", state_clip},
                                                  {"loop", true},
                                                  {"speed", 1},
                                                  {"transition_seconds", .4}}});
        states.validate();
        write(project / "Assets/Walk.states.json", states.document);
        catalog = AssetCatalog::open_project(project);
        catalog.add({states.asset(), "animation_states", "Assets/Walk.states.json", 1, {}});
        catalog.save(AssetCatalog::project_index(project));
        rejects([&] {
            prepare_asset_file_operation(
                project, {AssetFileAction::Duplicate, states.asset(), "Moved/wrong.json"}, rewrite);
        });
        const auto states_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, states.asset(), "Moved/Copy.states.json"},
            rewrite);
        transaction.commit(states_copy.changes, false);
        const AnimationStatesSource copied_states{read(project / "Moved/Copy.states.json")};
        copied_states.validate();
        check(copied_states.asset() == states_copy.result &&
                  copied_states.asset() != states.asset() &&
                  copied_states.document.at("states") == states.document.at("states") &&
                  copied_states.document.at("next_id") == 6,
              "State set duplicate changed local IDs or clip references");
        const auto states_move = prepare_asset_file_operation(
            project, {AssetFileAction::Move, states_copy.result, "Assets/Copy.states.json"},
            rewrite);
        transaction.commit(states_move.changes, false);
        check(read(project / "Assets/Copy.states.json") == copied_states.document,
              "State set move changed identities");
        EngineContext reference_world;
        Scene reference_scene(reference_world.world());
        auto retarget = AnimationRetargetSource::create(AssetId::generate());
        const auto target_rig = AssetId::generate(), source_joint = AssetId::generate(),
                   target_joint = AssetId::generate();
        retarget.document["source_skeleton"] = rig;
        retarget.document["source_clip"] = state_clip;
        retarget.document["target_skeleton"] = target_rig;
        retarget.document["entries"] =
            Json::array({{{"source", source_joint}, {"target", target_joint}}});
        retarget.document["extension"] = {{"retain", 17}};
        write(project / "Assets/Swing.retarget.json", retarget.document);
        catalog = AssetCatalog::open_project(project);
        const AssetRecord retarget_record{
            retarget.asset(), "animation_clip", "Assets/Swing.retarget.json", 1, {}};
        catalog.add(retarget_record);
        catalog.save(AssetCatalog::project_index(project));
        rejects([&] {
            prepare_asset_file_operation(
                project, {AssetFileAction::Duplicate, retarget.asset(), "Moved/wrong.json"},
                rewrite);
        });
        const auto retarget_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, retarget.asset(), "Moved/Copy.retarget.json"},
            rewrite);
        transaction.commit(retarget_copy.changes, false);
        auto retarget_expected = retarget.document;
        retarget_expected["asset_id"] = retarget_copy.result;
        check(retarget_copy.result != retarget.asset() &&
                  read(project / "Moved/Copy.retarget.json") == retarget_expected,
              "Retarget copy lost mapping/extensions or reused source identity");
        const auto retarget_move = prepare_asset_file_operation(
            project, {AssetFileAction::Move, retarget_copy.result, "Assets/Copy.retarget.json"},
            rewrite);
        transaction.commit(retarget_move.changes, false);
        check(read(project / "Assets/Copy.retarget.json") == retarget_expected,
              "Retarget move changed asset or joint identities");
        const std::array retarget_documents{
            AssetReferenceDocument{"Assets/Swing.retarget.json", retarget.document}};
        const auto retarget_impact =
            inspect_asset_references(reference_scene.schema(), retarget_documents,
                                     {rig, state_clip, target_rig, source_joint, target_joint});
        check(retarget_impact.references.size() == 5 && retarget_impact.uninspected.size() == 1 &&
                  retarget_impact.uninspected[0].find("extension") != std::string::npos,
              "Retarget source/target/clip/joint reference impact lost typed or opaque coverage");
        auto oversized_retarget = retarget.document;
        oversized_retarget["extension"] = std::string(animation_retarget_byte_limit, 'x');
        rejects([&] {
            rewrite(retarget_record, oversized_retarget.dump(), "Moved/TooLarge.retarget.json", {});
        });
        auto image = RenderTextureSource::create(AssetId::generate());
        image.document["width"] = 800;
        image.document["height"] = 400;
        image.document["extension"] = {{"retain", 17}};
        write(project / "Assets/Screen.rendertexture.json", image.document);
        auto dynamic_material = MaterialSource::create(AssetId::generate());
        dynamic_material.enable_render_textures();
        MaterialData image_values;
        image_values.model = "forge.gltf.metallic-roughness.v1";
        image_values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        dynamic_material.document["overrides"]["textures"]["baseColorTexture"] = {
            {"render_texture", image.asset()},
            {"slot", material_values_document(image_values).at("textures").at("baseColorTexture")}};
        write(project / "Assets/Screen.material.json", dynamic_material.document);
        catalog = AssetCatalog::open_project(project);
        catalog.add(
            {image.asset(), RenderTextureAsset::type, "Assets/Screen.rendertexture.json", 1, {}});
        catalog.add(
            {dynamic_material.asset(), MaterialAsset::type, "Assets/Screen.material.json", 1, {}});
        catalog.save(AssetCatalog::project_index(project));
        const auto image_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, image.asset(), "Moved/Screen.rendertexture.json"},
            rewrite);
        transaction.commit(image_copy.changes, false);
        auto image_expected = image.document;
        image_expected["asset_id"] = image_copy.result;
        check(image_copy.result != image.asset() &&
                  read(project / "Moved/Screen.rendertexture.json") == image_expected,
              "Camera output copy lost configuration/extensions or reused its source identity");
        const auto screen_copy = prepare_asset_file_operation(
            project,
            {AssetFileAction::Duplicate, dynamic_material.asset(), "Moved/Screen.material.json"},
            rewrite);
        transaction.commit(screen_copy.changes, false);
        auto screen_expected = dynamic_material.document;
        screen_expected["asset_id"] = screen_copy.result;
        check(read(project / "Moved/Screen.material.json") == screen_expected,
              "Material copy retargeted its shared camera output or lost source data");
        const std::array screen_documents{
            AssetReferenceDocument{"Assets/Screen.rendertexture.json", image.document},
            AssetReferenceDocument{"Assets/Screen.material.json", dynamic_material.document}};
        const auto output_impact =
            inspect_asset_references(reference_scene.schema(), screen_documents, {image.asset()});
        check(output_impact.references.size() == 1 && output_impact.uninspected.size() == 1 &&
                  output_impact.uninspected[0].find("extension") != std::string::npos &&
                  output_impact.references[0].expected_type == RenderTextureAsset::type,
              "Camera output reference review lost its Material-owned typed edge");
        const std::array documents{
            AssetReferenceDocument{"Assets/Walk.states.json", states.document}};
        const auto impact =
            inspect_asset_references(reference_scene.schema(), documents, {rig, state_clip});
        check(impact.references.size() == 2 && impact.uninspected.empty(),
              "State skeleton/clip impact review lost typed coverage");
        auto opaque_states = states.document;
        opaque_states["states"][0]["future"] = rig;
        const std::array opaque_documents{
            AssetReferenceDocument{"Assets/Walk.states.json", opaque_states}};
        const auto opaque_impact =
            inspect_asset_references(reference_scene.schema(), opaque_documents, {rig});
        check(!opaque_impact.uninspected.empty(),
              "State extensions falsely claimed complete reference coverage");
        auto mask = AnimationMaskSource::create(AssetId::generate());
        const auto bone = AssetId::generate();
        mask.document["skeleton"] = rig;
        mask.document["entries"] =
            Json::array({{{"joint", bone}, {"weight", .5}, {"descendants", true}}});
        write(project / "Assets/Upper.mask.json", mask.document);
        catalog = AssetCatalog::open_project(project);
        catalog.add({mask.asset(), AnimationMaskAsset::type, "Assets/Upper.mask.json", 1, {}});
        catalog.save(AssetCatalog::project_index(project));
        rejects([&] {
            prepare_asset_file_operation(
                project, {AssetFileAction::Duplicate, mask.asset(), "Moved/wrong.json"}, rewrite);
        });
        const auto mask_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, mask.asset(), "Moved/Copy.mask.json"}, rewrite);
        transaction.commit(mask_copy.changes, false);
        const AnimationMaskSource copied_mask{read(project / "Moved/Copy.mask.json")};
        copied_mask.validate();
        check(copied_mask.asset() == mask_copy.result && copied_mask.asset() != mask.asset() &&
                  copied_mask.document.at("entries") == mask.document.at("entries") &&
                  copied_mask.document.at("skeleton") == Json(rig),
              "Mask duplicate changed skeleton or durable joint references");
        const auto mask_move = prepare_asset_file_operation(
            project, {AssetFileAction::Move, mask_copy.result, "Assets/Copy.mask.json"}, rewrite);
        transaction.commit(mask_move.changes, false);
        check(read(project / "Assets/Copy.mask.json") == copied_mask.document,
              "Mask move changed identity or bone influences");
        const std::array mask_documents{
            AssetReferenceDocument{"Assets/Upper.mask.json", mask.document}};
        const auto mask_impact =
            inspect_asset_references(reference_scene.schema(), mask_documents, {rig, bone});
        check(mask_impact.references.size() == 2 && mask_impact.uninspected.empty(),
              "Mask skeleton/bone impact review lost typed coverage");
        auto opaque_mask = mask.document;
        opaque_mask["entries"][0]["future"] = rig;
        const std::array opaque_masks{
            AssetReferenceDocument{"Assets/Upper.mask.json", opaque_mask}};
        check(!inspect_asset_references(reference_scene.schema(), opaque_masks, {rig})
                   .uninspected.empty(),
              "Mask extensions falsely claimed complete reference coverage");
        auto space = AnimationBlendSpaceSource::create(AssetId::generate());
        space.document["skeleton"] = rig;
        const auto sample = AssetId::generate();
        space.document["samples"] =
            Json::array({{{"id", sample}, {"clip", state_clip}, {"position", 0}}});
        write(project / "Assets/Motion.blendspace.json", space.document);
        catalog = AssetCatalog::open_project(project);
        catalog.add({space.asset(),
                     AnimationBlendSpaceAsset::type,
                     "Assets/Motion.blendspace.json",
                     1,
                     {}});
        catalog.save(AssetCatalog::project_index(project));
        rejects([&] {
            prepare_asset_file_operation(
                project, {AssetFileAction::Duplicate, space.asset(), "Moved/wrong.json"}, rewrite);
        });
        const auto space_copy = prepare_asset_file_operation(
            project, {AssetFileAction::Duplicate, space.asset(), "Moved/Copy.blendspace.json"},
            rewrite);
        transaction.commit(space_copy.changes, false);
        const AnimationBlendSpaceSource copied_space{read(project / "Moved/Copy.blendspace.json")};
        copied_space.validate();
        check(copied_space.asset() == space_copy.result && copied_space.asset() != space.asset() &&
                  copied_space.document.at("samples") == space.document.at("samples") &&
                  copied_space.document.at("skeleton") == Json(rig),
              "Blend Space duplicate changed clip or stable sample identities");
        const auto space_move = prepare_asset_file_operation(
            project, {AssetFileAction::Move, space_copy.result, "Assets/Copy.blendspace.json"},
            rewrite);
        transaction.commit(space_move.changes, false);
        check(read(project / "Assets/Copy.blendspace.json") == copied_space.document,
              "Blend Space move changed source identity or samples");
        const std::array space_documents{
            AssetReferenceDocument{"Assets/Motion.blendspace.json", space.document}};
        const auto space_impact =
            inspect_asset_references(reference_scene.schema(), space_documents, {rig, state_clip});
        check(space_impact.references.size() == 2 && space_impact.uninspected.empty(),
              "Blend Space skeleton/clip impact lost typed coverage");
        auto opaque_space = space.document;
        opaque_space["samples"][0]["future"] = rig;
        const std::array opaque_spaces{
            AssetReferenceDocument{"Assets/Motion.blendspace.json", opaque_space}};
        check(!inspect_asset_references(reference_scene.schema(), opaque_spaces, {rig})
                   .uninspected.empty(),
              "Blend Space extensions falsely claimed complete reference coverage");
        {
            auto graph = AnimationGraphSource::create(AssetId::generate());
            const auto output = AssetId::generate();
            graph.document["skeleton"] = rig;
            graph.document["nodes"] = {
                {{"id", graph.asset()}, {"name", "Base"}, {"kind", "clip"}, {"clip", state_clip}},
                {{"id", state_clip}, {"name", "Overlay"}, {"kind", "clip"}, {"clip", state_clip}},
                {{"id", output},
                 {"name", "Layer"},
                 {"kind", "blend"},
                 {"first", graph.asset()},
                 {"second", state_clip},
                 {"mask", mask.asset()},
                 {"weight", .5}}};
            graph.document["output"] = output;
            graph.document["extension"] = {{"reference_like", graph.asset()}};
            write(project / "Assets/Motion.animgraph.json", graph.document);
            catalog = AssetCatalog::open_project(project);
            catalog.add(
                {graph.asset(), AnimationGraphAsset::type, "Assets/Motion.animgraph.json", 1, {}});
            catalog.save(AssetCatalog::project_index(project));
            rejects([&] {
                prepare_asset_file_operation(
                    project, {AssetFileAction::Duplicate, graph.asset(), "Moved/wrong.json"},
                    rewrite);
            });
            const auto graph_copy = prepare_asset_file_operation(
                project, {AssetFileAction::Duplicate, graph.asset(), "Moved/Copy.animgraph.json"},
                rewrite);
            transaction.commit(graph_copy.changes, false);
            const AnimationGraphSource copied_graph{read(project / "Moved/Copy.animgraph.json")};
            (void)copied_graph.compiled();
            check(copied_graph.asset() == graph_copy.result &&
                      copied_graph.asset() != graph.asset() &&
                      copied_graph.document.at("nodes") == graph.document.at("nodes") &&
                      copied_graph.document.at("extension") == graph.document.at("extension") &&
                      copied_graph.document.at("skeleton") == Json(rig),
                  "Graph duplication rewrote scoped nodes, external references or opaque payload");
            const auto graph_move = prepare_asset_file_operation(
                project, {AssetFileAction::Move, graph_copy.result, "Assets/Copy.animgraph.json"},
                rewrite);
            transaction.commit(graph_move.changes, false);
            check(read(project / "Assets/Copy.animgraph.json") == copied_graph.document,
                  "Graph move changed source identity, local links or metadata");
            auto clean_graph = graph.document;
            clean_graph.erase("extension");
            const std::array graph_documents{
                AssetReferenceDocument{"Assets/Motion.animgraph.json", clean_graph}};
            const auto graph_impact = inspect_asset_references(
                reference_scene.schema(), graph_documents, {rig, state_clip, mask.asset()});
            check(
                graph_impact.references.size() == 4 && graph_impact.uninspected.empty() &&
                    inspect_asset_references(reference_scene.schema(), graph_documents,
                                             {graph.asset()})
                        .references.empty(),
                "Graph reference review lost typed coverage or treated scoped UUID links as asset "
                "references");
            const std::array opaque_graphs{
                AssetReferenceDocument{"Assets/Motion.animgraph.json", graph.document}};
            check(inspect_asset_references(reference_scene.schema(), opaque_graphs, {rig})
                          .uninspected.size() == 1,
                  "Opaque graph metadata falsely claimed complete reference coverage");
            auto oversized_graph = graph.document;
            oversized_graph["extension"] = std::string(animation_graph_byte_limit, 'x');
            rejects([&] {
                rewrite(catalog.records().at(graph.asset()), oversized_graph.dump(),
                        "Moved/Large.animgraph.json", {});
            });
        }
        test_asset_file_service(project / "service");
        std::cout << "Asset file scene/family/move/delete/duplicate/publication tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
