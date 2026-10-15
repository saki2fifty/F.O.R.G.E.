#include "../src/editor/document.hpp"
#include <forge/animation_components.hpp>
#include <forge/authoring.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string create(Scene& scene, const char* name, const char* recipe) {
    return authoring_command(scene, "entity.create", {{"name", name}, {"recipe", recipe}})
        .at("selected");
}
std::string mapped(Scene& scene, const std::string& root, const std::string& member) {
    const auto saved = scene.document();
    for (const auto& item : saved.at("entities"))
        if (item.at("id") == root)
            return item.at("prefab_instance").at("members").at(member);
    throw std::runtime_error("Missing Apply test instance");
}
bool same_intent(Json left, Json right) {
    // Coordinated history deliberately advances immutable source revisions.
    for (auto* document : {&left, &right})
        for (auto& item : (*document)["entities"])
            if (item.contains("prefab_instance"))
                item["prefab_instance"]["revision"] = 0;
    return left == right;
}
void legacy_scalar_apply(const std::filesystem::path& root, unsigned version) {
    SceneDocument::create_project(root, "Legacy reference Apply");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    const auto original = create(scene, "Legacy controller", "animation.sequence");
    const auto child = create(scene, "Legacy track", "empty");
    authoring_command(scene, "component.add",
                      {{"entity", child}, {"component", "forge.transform_animator"}});
    scene.reparent_entity(child, original, ReparentMode::KeepLocal);
    auto source = create_prefab_source(scene, original);
    source.source["version"] = version;
    const EntityRef absolute{scene.asset_id(), EntityId::parse(original)};
    std::string member;
    for (auto& item : source.source["members"])
        if (item.at("name") == "Legacy track") {
            member = item.at("id");
            item["components"]["forge.transform_animator"]["sequence"] = absolute;
        }
    const auto asset = document.prefabs().create(scene, source, "Assets/Legacy.prefab.json");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
    const auto first_actor = mapped(scene, first, member),
               second_actor = mapped(scene, second, member);
    document.save();
    authoring_command(scene, "property.set",
                      {{"entity", first_actor},
                       {"component", "forge.transform_animator"},
                       {"field", "sequence_start_seconds"},
                       {"value", .75}});
    const auto candidate = document.prefabs().prepare_apply(scene, first);
    check(candidate.source.at("version") == version,
          "Unrelated scalar Apply silently migrated an older reference format");
    for (const auto& item : candidate.source.at("members"))
        if (item.at("id") == member)
            check(item.at("components").at("forge.transform_animator").at("sequence") ==
                      Json(absolute),
                  "Unrelated scalar Apply changed an existing absolute Scene link");
    document.apply_prefab(candidate, scene.revision());
    check(scene.entity(first_actor).get<TransformAnimator>().sequence == absolute &&
              scene.entity(second_actor).get<TransformAnimator>().sequence == absolute &&
              scene.entity(second_actor).get<TransformAnimator>().sequence_start_seconds == .75 &&
              !document.dirty(),
          "Legacy scalar Apply failed its durable reference/value preservation contract");
}
void test(const std::filesystem::path& root) {
    SceneDocument::create_project(root, "Portable Apply");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    const auto original = create(scene, "Rig", "animation.sequence");
    const auto child = create(scene, "Track", "empty");
    authoring_command(scene, "component.add",
                      {{"entity", child}, {"component", "forge.transform_animator"}});
    scene.reparent_entity(child, original, ReparentMode::KeepLocal);
    const auto source = create_prefab_source(scene, original);
    check(source.source.at("version") < 3, "Null reference unnecessarily upgraded source");
    std::string track;
    for (const auto& member : source.source.at("members"))
        if (member.at("name") == "Track")
            track = member.at("id");
    const auto asset = document.prefabs().create(scene, source, "Assets/Rig.prefab.json");
    const auto a = instantiate_prefab(scene, asset), b = instantiate_prefab(scene, asset);
    const auto ac = mapped(scene, a, track), bc = mapped(scene, b, track);
    document.save();
    const EntityRef a_ref{scene.asset_id(), EntityId::parse(a)};
    const EntityRef b_ref{scene.asset_id(), EntityId::parse(b)};
    auto bind = [&](EntityRef target) {
        authoring_command(scene, "property.set",
                          {{"entity", ac},
                           {"component", "forge.transform_animator"},
                           {"field", "sequence"},
                           {"value", target}});
    };
    bind(a_ref);
    const auto before = scene.document();
    const auto previous_source = document.prefabs().source(asset);
    const auto candidate = document.prefabs().prepare_apply(scene, a);
    check(candidate.source.at("version") == 3, "Apply failed to version member reference intent");
    for (const auto& member : candidate.source.at("members"))
        if (member.at("id") == track)
            check(member.at("components").at("forge.transform_animator").at("sequence") ==
                      Json(PrefabMemberRef{asset, source.root()}),
                  "Apply saved a Scene ref in the portable source");
    document.apply_prefab(candidate, scene.revision());
    check(!document.dirty() && read_json(document.path()) == scene.document() &&
              scene.entity(ac).get<TransformAnimator>().sequence == a_ref &&
              scene.entity(bc).get<TransformAnimator>().sequence == b_ref,
          "Coordinated Apply did not persist independent instance bindings");
    const auto applied = scene.document();
    check(scene.undo() && same_intent(scene.document(), before) &&
              scene.entity(bc).get<TransformAnimator>().sequence == EntityRef{},
          "Coordinated Undo did not restore scene/source reference intent");
    check(read_json(document.path()) == scene.document() &&
              document.prefabs().source(asset).at("version") == previous_source.at("version"),
          "Coordinated Undo did not restore the durable format contract");
    check(scene.redo() && same_intent(scene.document(), applied) &&
              scene.entity(bc).get<TransformAnimator>().sequence == b_ref,
          "Coordinated Redo did not restore portable source bindings");
    bind(b_ref);
    const auto external_intent = scene.document();
    const auto source_before_rejection = document.prefabs().source(asset);
    const auto disk_before_rejection = read_json(document.path());
    bool rejected = false;
    try {
        (void)document.prefabs().prepare_apply(scene, a);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && scene.document() == external_intent &&
              document.prefabs().source(asset) == source_before_rejection &&
              read_json(document.path()) == disk_before_rejection,
          "External reference Apply rejection changed a durable file or current intent");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide isolated Apply project directory");
        std::filesystem::remove_all(argv[1]);
        test(std::filesystem::absolute(argv[1]));
        for (unsigned version : {1u, 2u})
            legacy_scalar_apply(
                std::filesystem::absolute(argv[1]) / ("legacy" + std::to_string(version)), version);
        std::cout << "Portable reference coordinated Apply passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
