#include "../src/authored_component.hpp"
#include "editor/prefabs.hpp"
#include "prefab_variant_fixture.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]) / forge::AssetId::generate().str();
        std::filesystem::create_directories(root.parent_path());
        forge::SceneDocument::create_project(root, "Prefab source draft");
        forge::EngineContext engine(forge::WorldRole::Authoring, false,
                                    {forge::test::variant_controls_module()});
        forge::Scene scene(engine.world());
        forge::SceneDocument project(scene);
        project.open_project(root);
        const auto parent =
            forge::authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Root"}})
                .at("selected")
                .get<std::string>();
        const auto child_entity =
            forge::authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Child"}})
                .at("selected")
                .get<std::string>();
        forge::authoring_command(scene, "entity.reparent",
                                 {{"entity", child_entity}, {"parent", parent}});
        const auto id = project.prefabs().create(scene, forge::create_prefab_source(scene, parent),
                                                 "Assets/Tree.prefab.json");
        forge::PrefabEditor editor;
        editor.edit_source(scene, project, id);
        auto candidate = editor.migration_document();
        auto child = candidate.at("members").back();
        child.erase("parent");
        candidate["root"] = child.at("id");
        candidate["members"] = forge::Json::array({child});
        editor.edit_draft(candidate);
        // A subsequent property edit must reach the surviving selected member.
        editor.rename_member("Replacement root");
        if (editor.migration_document().at("members").front().at("name") != "Replacement root")
            throw std::runtime_error(
                "Removing the selected prefab member left a stale editor selection");
        const auto before = editor.migration_document();
        candidate = before;
        candidate["asset_id"] = forge::AssetId::generate();
        bool rejected = false;
        try {
            editor.edit_draft(candidate);
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected || editor.migration_document() != before)
            throw std::runtime_error("Invalid prefab identity changed the shared source draft");
        editor.resolve_close(forge::ui::DraftResolution::Discard, scene, project);
        const auto variant =
            project.prefabs().create_variant(scene, id, "Assets/Variant.prefab.json");
        editor.edit_source(scene, project, variant);
        const auto empty = editor.migration_document();
        const auto initial_view = editor.source_view(scene.schema());
        editor.rename_member(initial_view.at("members").front().at("name"));
        if (!editor.dirty())
            throw std::runtime_error("Equal-value variant name lost its explicit intent");
        editor.revert_name();
        if (editor.migration_document() != empty || editor.dirty())
            throw std::runtime_error("Name Revert did not restore clean inherited intent");
        editor.set_property("forge.tint", "r", .2);
        editor.set_property("forge.tint", "g", .8);
        editor.revert_property("forge.tint", "r");
        const auto mask = editor.migration_document().at("overrides").front();
        if (!mask.at("components").empty() ||
            mask.at("property_overrides").at("forge.tint") != forge::Json({{"g", .8}}))
            throw std::runtime_error("Variant field editing flattened inherited values");
        editor.revert_component("forge.tint");
        editor.set_property("forge.local_translation", "x", 3);
        const auto position = editor.migration_document().at("overrides").front();
        if (position.contains("property_overrides") ||
            position.at("components").at("forge.local_translation").at("x") != 3)
            throw std::runtime_error("Variant transform edit failed to retain a complete channel");
        editor.revert_component("forge.local_translation");
        if (editor.migration_document() != empty)
            throw std::runtime_error("Component Revert did not restore inheritance");
        editor.rename_member("Variant draft");
        const auto raw_draft = editor.migration_document();
        auto base = project.prefabs().source(id);
        base["members"][0]["name"] = "Updated base";
        project.prefabs().publish(scene, project.prefabs().source(id), base);
        if (editor.publish(scene, project))
            throw std::runtime_error("A stale variant base review was published");
        editor.rebase_draft(scene, project);
        if (editor.migration_document() != raw_draft || !editor.publish(scene, project))
            throw std::runtime_error("Rebase discarded draft intent or failed valid publication");
        const auto published = project.prefabs().source(variant);
        const auto instance = forge::instantiate_prefab(scene, variant);
        project.save();
        forge::authoring_command(scene, "entity.rename",
                                 {{"entity", instance}, {"name", "Applied variant"}});
        auto apply = project.prefabs().prepare_apply(scene, instance);
        project.apply_prefab(apply, scene.revision());
        if (project.prefabs().source(variant).at("overrides").front().at("name") !=
            "Applied variant")
            throw std::runtime_error("Coordinated variant Apply failed");
        scene.undo();
        if (project.prefabs().source(variant).at("overrides") != published.at("overrides"))
            throw std::runtime_error("Variant Apply Undo lost source intent");
        scene.redo();
        if (project.prefabs().source(variant).at("overrides").front().at("name") !=
                "Applied variant" ||
            project.prefabs().source(id).at("members").front().at("name") != "Updated base")
            throw std::runtime_error("Variant Apply Redo changed the base or lost history");
        editor.resolve_close(forge::ui::DraftResolution::Discard, scene, project);
        const auto& codec = engine.world().authored_codecs().front();
        const auto current_base = project.prefabs().source(id);
        base = current_base;
        for (auto& member : base["members"])
            if (member.at("id") == base.at("root"))
                member["components"][codec.key()] = codec.defaults();
        project.prefabs().publish(scene, current_base, base);
        const auto custom =
            project.prefabs().create_variant(scene, id, "Assets/CustomVariant.prefab.json");
        editor.edit_source(scene, project, custom);
        editor.set_property(codec.key(), "speed", 90);
        const auto admitted = editor.migration_document();
        if (admitted.at("overrides")[0].at("property_overrides").at(codec.key()).at("$forge") !=
            codec.stamp())
            throw std::runtime_error(
                "Custom source property edit omitted its admitted schema stamp");
        auto stale = admitted;
        stale["overrides"][0]["property_overrides"][codec.key()]["$forge"]["digest"] =
            "unavailable-schema";
        editor.edit_draft(stale);
        rejected = false;
        try {
            editor.set_property(codec.key(), "speed", 100);
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected || editor.migration_document() != stale)
            throw std::runtime_error(
                "Typed property editing readmitted or changed an unavailable variant mask");
        editor.rename_member("Unrelated name edit");
        if (!editor.publish(scene, project) || project.prefabs()
                                                       .source(custom)
                                                       .at("overrides")[0]
                                                       .at("property_overrides")
                                                       .at(codec.key())
                                                       .at("$forge")
                                                       .at("digest") != "unavailable-schema")
            throw std::runtime_error("Unrelated source publication discarded opaque custom intent");
        std::cout << "Prefab source selection and failed identity edit passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
