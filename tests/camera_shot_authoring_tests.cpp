#include "camera_shot_authoring.hpp"
#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    EngineContext engine;
    Scene scene{engine.world()};
    std::string controller, camera, shot;
    EntityRef group;
    Fixture() {
        controller = create("animation.sequence");
        camera = create("render.camera");
        group = {scene.asset_id(), EntityId::parse(controller)};
        fields(controller, "forge.animation_sequence", {{"duration", 12.}});
        shot = add(1, 5, 1, 3);
    }
    std::string create(const char* recipe) {
        return authoring_command(scene, "entity.create", {{"recipe", recipe}})
            .at("selected")
            .get<std::string>();
    }
    void fields(const std::string& id, const char* type, const Json& values) {
        authoring_command(scene, "property.set_many",
                          {{"entity", id}, {"component", type}, {"values", values}});
    }
    std::string add(double start, double end, double blend = 0, unsigned curve = 0,
                    bool enabled = true) {
        return authoring_command(
                   scene, "entity.create",
                   {{"recipe", "animation.camera_shot"},
                    {"component_values",
                     {{"sequence", group},
                      {"camera", EntityRef{scene.asset_id(), EntityId::parse(camera)}},
                      {"start_seconds", start},
                      {"end_seconds", end},
                      {"blend_in_seconds", blend},
                      {"blend_curve", curve},
                      {"enabled", enabled}}}})
            .at("selected")
            .get<std::string>();
    }
    template <class Action> void refused(Action&& action) {
        const auto before = scene.document();
        const auto revision = scene.revision();
        const auto undo = scene.can_undo(), redo = scene.can_redo();
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == before && scene.revision() == revision &&
                  scene.can_undo() == undo && scene.can_redo() == redo,
              "Refused Camera Shot command published source or history");
    }
    std::string edit(const char* operation, double time) {
        return authoring_command(scene, operation, {{"entity", shot}, {"time", time}})
            .at("selected")
            .get<std::string>();
    }
    void history(const Json& before, Json after) {
        check(scene.undo() && scene.document() == before, "Shot operation was not one Scene Undo");
        check(scene.redo() && scene.document() == after, "Shot Redo changed source identity/data");
        check(scene.undo() && scene.document() == before, "Shot repeat Undo lost the source");
    }
};
Json& row(Json& document, const std::string& id) {
    for (auto& actor : document["entities"])
        if (actor.at("id").get<std::string>() == id)
            return actor;
    throw std::runtime_error("Missing source oracle row");
}
void split(const std::filesystem::path& scratch) {
    Fixture f;
    for (unsigned curve = 0; curve != 4; ++curve) {
        f.fields(f.shot, "forge.camera_shot", {{"blend_curve", curve}});
        const auto before = f.scene.document();
        const auto original = f.scene.entity(f.shot).get<CameraShot>();
        const auto created = f.edit("camera_shot.split", 3);
        const auto after = f.scene.document();
        const auto first = f.scene.entity(f.shot).get<CameraShot>();
        const auto second = f.scene.entity(created).get<CameraShot>();
        auto expected_first = original;
        expected_first.end_seconds = 3;
        auto expected_second = original;
        expected_second.start_seconds = 3;
        expected_second.blend_in_seconds = 0;
        check(created != f.shot && first == expected_first && second == expected_second &&
                  after.at("entities").size() == before.at("entities").size() + 1,
              "Split changed camera/curve/state/span or reused source identity");
        auto unchanged = after;
        unchanged["entities"].erase(unchanged["entities"].end() - 1);
        row(unchanged, f.shot)["components"]["forge.camera_shot"]["end_seconds"] =
            original.end_seconds;
        check(unchanged == before, "Split changed unrelated source fields or order");
        std::filesystem::create_directories(scratch);
        const auto path = scratch / "split.scene.json";
        f.scene.save(path);
        Scene reopened(f.engine.world());
        reopened.load(path);
        check(reopened.document() == after, "Saved split did not reopen with stable identity");
        f.history(before, after);
    }
    for (double time : {0., 1., 1.5, 5., 6.})
        f.refused([&] { f.edit("camera_shot.split", time); });
    f.fields(f.shot, "forge.camera_shot", {{"enabled", false}});
    const auto before = f.scene.document();
    const auto created = f.edit("camera_shot.split", 2);
    check(!f.scene.entity(created).get<CameraShot>().enabled &&
              f.scene.entity(f.shot).get<CameraShot>().end_seconds == 2,
          "Split changed disabled state or rejected the exact incoming blend boundary");
    f.history(before, f.scene.document());
}
void duplicate() {
    Fixture f;
    f.fields(f.shot, "forge.camera_shot",
             {{"start_seconds", 0.}, {"end_seconds", 2.}, {"blend_in_seconds", .5}});
    f.add(3, 5);
    const auto before = f.scene.document();
    const auto original = f.scene.entity(f.shot).get<CameraShot>();
    const auto created = f.edit("camera_shot.duplicate", 0);
    auto expected = original;
    expected.start_seconds = 5;
    expected.end_seconds = 7;
    check(f.scene.entity(created).get<CameraShot>() == expected &&
              f.scene.entity(f.shot).get<CameraShot>() == original,
          "Duplicate shortened its span/blend or did not skip an insufficient gap");
    f.history(before, f.scene.document());
    const auto at_end = f.edit("camera_shot.duplicate", 10);
    check(f.scene.entity(at_end).get<CameraShot>().end_seconds == 12,
          "Duplicate refused the exact controller end boundary");
    f.history(before, f.scene.document());
    for (double time : {-1., 11., 13.})
        f.refused([&] { f.edit("camera_shot.duplicate", time); });
    f.fields(f.shot, "forge.camera_shot", {{"enabled", false}});
    const auto disabled = f.edit("camera_shot.duplicate", 0);
    check(!f.scene.entity(disabled).get<CameraShot>().enabled,
          "Duplicate enabled a disabled source shot");
}
void extensions() {
    Fixture f;
    auto document = f.scene.document();
    row(document, f.shot)["components"]["forge.camera_shot"]["project_note"] = {
        {"keep", "opaque source data"}, {"values", {1, 2, 3}}};
    f.scene.edit(document);
    for (const auto* operation : {"camera_shot.split", "camera_shot.duplicate"}) {
        const auto before = f.scene.document();
        const auto created =
            f.edit(operation, operation == std::string_view("camera_shot.split") ? 3 : 6);
        auto after = f.scene.document();
        check(row(after, created).at("components").at("forge.camera_shot").at("project_note") ==
                  row(document, f.shot).at("components").at("forge.camera_shot").at("project_note"),
              "Shot operation discarded opaque component extensions");
        f.history(before, after);
    }
}
void outgoing_blend() {
    Fixture f;
    f.fields(f.shot, "forge.camera_shot",
             {{"start_seconds", 0.}, {"end_seconds", 2.}, {"blend_in_seconds", 0.}});
    const auto alternate = f.create("render.camera");
    f.fields(alternate, "forge.camera", {{"projection", unsigned(CameraProjection::Orthographic)}});
    const auto next = f.add(4, 6, 1);
    f.fields(next, "forge.camera_shot",
             {{"camera", EntityRef{f.scene.asset_id(), EntityId::parse(alternate)}}});
    f.refused([&] { f.edit("camera_shot.duplicate", 2); });
    f.fields(next, "forge.camera_shot", {{"blend_in_seconds", 0.}});
    const auto before = f.scene.document();
    const auto created = f.edit("camera_shot.duplicate", 2);
    const auto copy = f.scene.entity(created).get<CameraShot>();
    check(copy.start_seconds == 2 && copy.end_seconds == 4,
          "Duplicate refused a hard cut between different camera projections");
    f.history(before, f.scene.document());
}
void limits_and_composition() {
    Fixture f;
    f.fields(f.controller, "forge.animation_sequence", {{"duration", 1024.}});
    f.fields(f.shot, "forge.camera_shot", {{"end_seconds", 2.}, {"blend_in_seconds", 0.}});
    auto document = f.scene.document();
    const auto prototype = row(document, f.shot);
    for (unsigned i = 1; i < 256; ++i) {
        auto copy = prototype;
        copy["id"] = EntityId::generate().str();
        copy["components"]["forge.camera_shot"]["start_seconds"] = 1. + 2. * i;
        copy["components"]["forge.camera_shot"]["end_seconds"] = 2. + 2. * i;
        document["entities"].push_back(copy);
    }
    f.scene.edit(document);
    f.refused([&] { f.edit("camera_shot.split", 1.5); });
    f.refused([&] { f.edit("camera_shot.duplicate", 600); });
    document["entities"].erase(document["entities"].end() - 1);
    f.scene.edit(document);
    const auto before = f.scene.document();
    f.edit("camera_shot.split", 1.5);
    f.history(before, f.scene.document());

    Fixture lens;
    lens.fields(lens.shot, "forge.camera_shot",
                {{"start_seconds", 0.}, {"end_seconds", 2.}, {"blend_in_seconds", .5}});
    const auto alternate = lens.create("render.camera");
    lens.fields(alternate, "forge.camera",
                {{"projection", unsigned(CameraProjection::Orthographic)}});
    const auto previous = lens.add(4, 6);
    lens.fields(previous, "forge.camera_shot",
                {{"camera", EntityRef{lens.scene.asset_id(), EntityId::parse(alternate)}}});
    lens.refused([&] { lens.edit("camera_shot.duplicate", 6); });
    auto camera = lens.scene.entity(lens.camera).get<Camera>();
    camera.render_texture = {AssetId::generate()};
    lens.fields(lens.camera, "forge.camera",
                {{"render_texture",
                  detail::encode_builtin_value(detail::Value{camera}).at("render_texture")}});
    lens.refused([&] { lens.edit("camera_shot.split", 1); });

    Fixture director;
    const auto other_controller = director.create("animation.sequence");
    director.fields(other_controller, "forge.animation_sequence", {{"duration", 12.}});
    const auto other = director.add(6, 7);
    director.fields(
        other, "forge.camera_shot",
        {{"sequence", EntityRef{director.scene.asset_id(), EntityId::parse(other_controller)}}});
    director.refused([&] { director.edit("camera_shot.split", 3); });
    director.refused([&] { director.edit("camera_shot.duplicate", 8); });
    director.fields(other_controller, "forge.animation_sequence", {{"enabled", false}});
    director.edit("camera_shot.split", 3);
}
void inherited(const std::filesystem::path& scratch) {
    Fixture f;
    for (const auto& id : {f.camera, f.shot})
        authoring_command(f.scene, "entity.reparent", {{"entity", id}, {"parent", f.controller}});
    const auto definition = create_prefab_source(f.scene, f.controller);
    auto document = f.scene.document();
    document["entities"] = Json::array();
    f.scene.reset(document);
    f.scene.set_prefab_sources({{definition.asset(), definition.source}});
    instantiate_prefab(f.scene, definition.asset());
    const auto effective = f.scene.effective_document();
    for (const auto& entity : effective.at("entities"))
        if (entity.at("components").contains("forge.camera_shot"))
            f.shot = entity.at("id").get<std::string>();
    auto stored = f.scene.document();
    const auto& stored_shot = row(stored, f.shot);
    const bool stored_component =
        stored_shot.value("components", Json::object()).contains("forge.camera_shot");
    std::cout << "Prefab Shot native owns=" << f.scene.entity(f.shot).owns<CameraShot>()
              << " stored component=" << stored_component << '\n';
    check(f.scene.entity(f.shot).has<CameraShot>() && stored_shot.contains("prefab_member") &&
              !stored_component,
          "Prefab Shot fixture has unexpected authored component intent");
    for (const auto* operation : {"camera_shot.split", "camera_shot.duplicate"}) {
        const auto before = f.scene.document();
        const auto created =
            f.edit(operation, operation == std::string_view("camera_shot.split") ? 3 : 6);
        check(f.scene.entity(created).owns<CameraShot>() &&
                  f.scene.prefab_sources().at(definition.asset()) == definition.source,
              "Shot operation changed its Prefab source or lost the new owned component");
        const auto after = f.scene.document();
        const auto path = scratch / "prefab-shot.scene.json";
        f.scene.save(path);
        Scene reopened(f.engine.world());
        reopened.set_prefab_sources(f.scene.prefab_sources());
        reopened.load(path);
        check(reopened.document() == after,
              "Prefab Shot operation lost stored source intent on save/reopen");
        f.history(before, after);
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Camera Shot authoring scratch directory");
        split(std::filesystem::u8path(argv[1]));
        duplicate();
        extensions();
        outgoing_blend();
        limits_and_composition();
        inherited(std::filesystem::u8path(argv[1]));
        std::cout << "Camera Shot source split/duplicate/history checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
