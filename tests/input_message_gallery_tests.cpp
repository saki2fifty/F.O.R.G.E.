#include "../samples/projects/FeatureGallery/CodeExamples/InputMessageTriggers.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "runtime_package.hpp"
#include "standalone_manifest.hpp"
#include <algorithm>
#include <array>
#include <forge/authoring.hpp>
#include <forge/input_message.hpp>
#include <forge/project.hpp>
#include <forge/render_components.hpp>
#include <iostream>
using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/159-input-message-triggers.scene.json";
constexpr auto release_action = "8c68fc99-bdb7-4bf4-9b91-dae060349ff0";
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Trigger Gallery already exists");
    ProjectSettings settings(root);
    auto document = settings.document();
    document["input"]["actions"].push_back(
        {{"id", gallery_input_triggers::axis1},
         {"name", "Message height"},
         {"kind", "axis1"},
         {"bindings", Json::array({{{"control", "key.q"}, {"x", -1}},
                                   {{"control", "key.e"}},
                                   {{"control", "pad.right_x"}}})}});
    document["input"]["actions"].push_back(
        {{"id", gallery_input_triggers::axis2},
         {"name", "Message position"},
         {"kind", "axis2"},
         {"bindings", Json::array({{{"control", "key.a"}, {"x", -1}, {"y", 0}},
                                   {{"control", "key.d"}, {"x", 1}, {"y", 0}},
                                   {{"control", "key.w"}, {"x", 0}, {"y", 1}},
                                   {{"control", "key.s"}, {"x", 0}, {"y", -1}},
                                   {{"control", "pad.left_x"}, {"x", 1}, {"y", 0}},
                                   {{"control", "pad.left_y"}, {"x", 0}, {"y", -1}}})}});
    settings.save(document);
    WorldContext world;
    Scene scene(world);
    auto initial = scene.snapshot();
    initial["asset_id"] = gallery_input_triggers::scene;
    scene.replace(initial);
    const auto create = [&](const char* name, LocalTranslation position) {
        const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                            .at("selected")
                            .get<std::string>();
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>(position);
        return entity;
    };
    create("Release Space - toggle height", {-5, .5, 0})
        .set<Primitive>({0})
        .set<Tint>({.95f, .5f, .1f})
        .set<InputMessage>({true, release_action, "forge.local_translation",
                            std::uint32_t(InputMessageTrigger::Released)});
    create("Changed Q/E or right stick - height", {-2, 1.5, 0})
        .set<Primitive>({0})
        .set<Tint>({.2f, .7f, .95f})
        .set<InputMessage>({true, gallery_input_triggers::axis1, "forge.local_translation",
                            std::uint32_t(InputMessageTrigger::Changed)});
    create("Changed W/A/S/D or left stick - position", {1.5, 1.5, 0})
        .set<Primitive>({0})
        .set<Tint>({.3f, .9f, .45f})
        .set<InputMessage>({true, gallery_input_triggers::axis2, "forge.local_translation",
                            std::uint32_t(InputMessageTrigger::Changed)});
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 8;
    camera.aspect = 16. / 9.;
    create("Game camera", {-1, 1.5, -15}).set<Camera>(camera).set<Primitive>({4});
    Light light;
    light.intensity = 3;
    create("Sun", {0, 6, -5}).set<Light>(light).set<Primitive>({4});
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
}
void inspect(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    ProjectSettings settings(root);
    const auto input = settings.input();
    WorldContext world;
    Scene scene(world);
    scene.load(root / scene_path);
    check(scene.asset_id().str() == gallery_input_triggers::scene,
          "Trigger Scene identity changed");
    unsigned sources = 0;
    const auto authored = scene.document();
    for (const auto& row : authored.at("entities")) {
        const auto& values = row.at("components");
        if (!values.contains("forge.input_message"))
            continue;
        ++sources;
        const auto& message = values.at("forge.input_message");
        const auto action = ActionId::parse(message.at("action").get<std::string>());
        const auto definition =
            std::find_if(input.actions().begin(), input.actions().end(),
                         [&](const auto& candidate) { return candidate.id == action; });
        check(definition != input.actions().end() &&
                  message.at("payload") == "forge.local_translation" &&
                  values.contains("forge.local_translation"),
              "Trigger source route is incomplete");
        check(message.at("trigger") == (definition->kind == ActionKind::Digital ? 1 : 2),
              "Trigger does not match its Gallery action kind");
    }
    check(sources == 3, "Trigger Gallery source count differs");
    std::filesystem::create_directories(scratch);
    const auto destination = scratch / AssetId::generate().str();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{destination};
    const std::array roots{scene.asset_id()};
    const RuntimePackageTarget target{"linux", "none"};
    package_runtime_content(root, destination, roots, target, {}, {}, scene.schema());
    const auto catalog = open_runtime_content(destination, target);
    const auto& record = catalog.records().at(scene.asset_id());
    check(record.metadata.at("forge.runtime_document").at("runtime_features") ==
              Json{{"input_message_triggers", input_message_trigger_profile_version}},
          "Cooked trigger Scene omitted its runtime profile");
    validate_runtime_compatibility(
        catalog, {{"runtime_input_message_triggers", input_message_trigger_profile_version}});
    bool rejected = false;
    try {
        validate_runtime_compatibility(catalog, Json::object());
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Cooked trigger Scene admitted an older runtime kit");
    const auto packaged = asset_detail::parse_bounded_json(
        asset_detail::read_bytes(ProjectPaths(destination).resolve(record.source), 1024 * 1024),
        1024 * 1024);
    WorldContext loaded;
    Scene restored(loaded);
    restored.replace(packaged);
    check(restored.document() == scene.document(),
          "Source-free trigger Scene changed authored values");
    std::cout << "Trigger Gallery: native Scene/input admission, source-free content and kit "
                 "compatibility passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author")
            author(std::filesystem::absolute(argv[2]));
        else if (argc == 4 && std::string_view(argv[1]) == "--inspect")
            inspect(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
        else
            throw std::runtime_error("Need --author project or --inspect project scratch");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
