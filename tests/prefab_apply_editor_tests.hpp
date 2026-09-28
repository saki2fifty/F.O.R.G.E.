#pragma once
#include "document.hpp"
#include <forge/authoring.hpp>
void require(bool condition, const char* message);
inline void test_prefab_apply_documents() {
    const auto scratch =
        std::filesystem::current_path() / ("apply-editor-" + forge::AssetId::generate().str());
    std::filesystem::create_directory(scratch);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(p, e);
        }
    } cleanup{scratch};
    const auto project = scratch / "Game";
    forge::SceneDocument::create_project(project, "Apply game");
    forge::WorldContext world;
    forge::Scene scene(world);
    forge::SceneDocument document(scene);
    document.open_project(project);
    const auto original = forge::authoring_command(scene, "entity.create", {{"name", "Reusable"}})
                              .at("selected")
                              .get<std::string>();
    auto source = forge::create_prefab_source(scene, original);
    const auto child_member = forge::PrefabMemberId::generate();
    source.source["members"].push_back(
        {{"id", child_member},
         {"name", "Child"},
         {"parent", source.root()},
         {"components", source.source.at("members")[0].at("components")}});
    const auto asset = document.prefabs().create(scene, source, "Assets/Sample.prefab.json");
    const auto a = forge::instantiate_prefab(scene, asset),
               b = forge::instantiate_prefab(scene, asset);
    std::string ac, bc;
    const auto mapped = scene.document();
    for (const auto& row : mapped.at("entities")) {
        if (row.at("id") == a)
            ac = row.at("prefab_instance").at("members").at(child_member.str());
        if (row.at("id") == b)
            bc = row.at("prefab_instance").at("members").at(child_member.str());
    }
    document.save();
    const auto source_before = document.prefabs().source(asset);
    forge::authoring_command(scene, "transform.position",
                             {{"entity", a}, {"value", {{"x", 4}, {"y", 2}, {"z", 0}}}});
    forge::authoring_command(scene, "transform.scale",
                             {{"entity", b}, {"value", {{"x", 2}, {"y", 2}, {"z", 2}}}});
    forge::authoring_command(scene, "transform.position",
                             {{"entity", ac}, {"value", {{"x", 7}, {"y", 1}, {"z", 0}}}});
    // Equal-value independent rotation intent and opaque payload survive correctly.
    auto intent = scene.document();
    for (auto& row : intent["entities"])
        if (row.at("id") == a) {
            row["components"]["forge.local_rotation"] =
                source_before.at("members")[0].at("components").at("forge.local_rotation");
            row["property_overrides"]["forge.tint"] = {{"r", .25}};
            row["components"]["project.unknown"] = {{"opaque", 17}};
        }
    scene.edit(intent);
    require(document.dirty(), "Fixture should have dirty scene changes");
    const auto before = scene.document();
    const auto change = document.prefabs().prepare_apply(scene, a);
    require(change.overrides == 4, "Apply lost independent/equal-value channel intent");
    const auto stable_revision = scene.revision();
    auto invalid = change;
    invalid.source["members"][0]["components"]["forge.local_scale"] = {
        {"x", 1e20}, {"y", 1}, {"z", 1}};
    bool failed_candidate = false;
    try {
        document.apply_prefab(invalid, stable_revision);
    } catch (const std::exception&) {
        failed_candidate = true;
    }
    require(failed_candidate && scene.document() == before && scene.revision() == stable_revision &&
                document.prefabs().source(asset) == source_before && scene.can_undo(),
            "Invalid Apply changed source, scene or existing history");
    document.apply_prefab(change, scene.revision());
    const auto applied = scene.document();
    require(!scene.entity(ac).owns<forge::LocalTranslation>() &&
                scene.entity(bc).get<forge::LocalTranslation>().x == 7,
            "Apply did not reconcile structured child overrides");
    for (const auto& row : applied.at("entities"))
        if (row.at("id") == a)
            require(!row.value("property_overrides", forge::Json::object()).contains("forge.tint"),
                    "Apply retained property intent");
    require(!document.dirty() && forge::read_json(document.path()) == applied,
            "Apply did not save the complete dirty scene");
    require(!scene.entity(a).owns<forge::LocalTranslation>() &&
                !scene.entity(a).owns<forge::LocalRotation>() &&
                scene.entity(b).owns<forge::LocalScale>() &&
                scene.entity(b).get<forge::LocalTranslation>().x == 4,
            "Apply did not remove selected channel intent or preserve another instance override");
    for (const auto& row : applied.at("entities"))
        if (row.at("id") == a)
            require(row.at("components").at("project.unknown").at("opaque") == 17,
                    "Apply rewrote opaque payload");
    require(scene.undo(), "Apply Undo missing");
    require(document.prefabs().source(asset).at("revision") == 3 &&
                scene.entity(a).owns<forge::LocalTranslation>() &&
                scene.entity(a).owns<forge::LocalRotation>() && !document.dirty(),
            "Apply Undo did not restore intent and advance revision durably");
    require(scene.redo() && document.prefabs().source(asset).at("revision") == 4 &&
                !scene.entity(a).owns<forge::LocalTranslation>(),
            "Apply Redo failed");
    const auto good = scene.snapshot();
    const auto good_source = document.prefabs().source(asset);
    forge::atomic_write(document.path(), "{}");
    bool rejected = false;
    try {
        scene.undo();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && scene.can_undo() && scene.snapshot() == good &&
                document.prefabs().source(asset) == good_source,
            "External scene conflict consumed history or mutated prefab/world");
    forge::atomic_write(document.path(), scene.document().dump(2));
    auto external = good_source;
    external["revision"] = 5;
    external["members"][0]["name"] = "External";
    forge::atomic_write(project / "Assets/Sample.prefab.json", external.dump(2));
    rejected = false;
    try {
        scene.undo();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && scene.can_undo() && scene.snapshot() == good &&
                document.prefabs().source(asset) == external,
            "External source conflict was overwritten by Undo");
    forge::atomic_write(project / "Assets/Sample.prefab.json", good_source.dump(2));
    auto direct = good_source;
    direct["members"][0]["name"] = "Direct publish";
    document.prefabs().publish(scene, good_source, direct);
    require(!scene.can_undo() && !scene.can_redo(),
            "Direct source publish must keep its history boundary");
    document.save();
    document.open_scene(document.path());
    require(scene.entity(b).owns<forge::LocalScale>(),
            "Apply round trip erased other instance intent");
    document.new_scene();
    const auto untitled = forge::instantiate_prefab(scene, asset);
    forge::authoring_command(scene, "transform.position",
                             {{"entity", untitled}, {"value", {{"x", 1}, {"y", 1}, {"z", 1}}}});
    const auto untitled_candidate = document.prefabs().prepare_apply(scene, untitled);
    rejected = false;
    try {
        document.apply_prefab(untitled_candidate, scene.revision());
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Untitled scene Apply should require Save As");
}
