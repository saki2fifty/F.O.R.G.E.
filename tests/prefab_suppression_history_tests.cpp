#include "editor/document.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation_components.hpp>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Conflicting source5 operation was admitted");
}
void exercise(const std::filesystem::path& root) {
    std::filesystem::create_directories(root.parent_path());
    SceneDocument::create_project(root, "Prefab component suppression");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto original = authoring_command(scene, "entity.create", {{"name", "Base"}})
                              .at("selected")
                              .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", original}, {"component", "forge.navigation_surface"}});
    const auto definition = create_prefab_source(scene, original);
    const auto base = project.prefabs().create(scene, definition, "Assets/Base.prefab.json");
    const auto variant =
        project.prefabs().create_variant(scene, base, "Assets/Variant.prefab.json");
    const auto first = instantiate_prefab(scene, variant);
    const auto second = instantiate_prefab(scene, variant);
    project.save();
    const auto expected = project.prefabs().source(variant);
    const auto candidate =
        PrefabDocument(expected)
            .with_component_suppression(definition.root(), "forge.navigation_surface", true)
            .source;
    const auto bases = project.prefabs().base_sources(scene, expected);
    project.prefabs().publish(scene, expected, candidate, bases);
    const auto suppressed = project.prefabs().source(variant);
    check(!scene.entity(first).has<NavigationSurface>() &&
              !scene.entity(second).has<NavigationSurface>() && suppressed.at("version") == 5 &&
              !scene.can_undo(),
          "Direct source5 publication did not reconcile instances/history");
    rejects([&] { project.prefabs().publish(scene, expected, candidate, bases); });
    const auto saved_base = project.prefabs().source(base);
    authoring_command(scene, "component.add",
                      {{"entity", first}, {"component", "forge.navigation_surface"}});
    const auto review = project.prefabs().prepare_apply(scene, first);
    check(review.source.at("version") == 5 &&
              review.source.at("overrides")[0]
                  .at("components")
                  .contains("forge.navigation_surface") &&
              !review.source.at("overrides")[0].contains("suppressed_components"),
          "Apply did not replace suppression with admitted whole-component intent");
    project.apply_prefab(review, scene.revision());
    check(scene.entity(first).has<NavigationSurface>() &&
              scene.entity(second).has<NavigationSurface>() &&
              project.prefabs().source(base) == saved_base,
          "Source5 Apply changed the base or missed other instances");
    const auto applied_revision =
        project.prefabs().source(variant).at("revision").get<std::uint64_t>();
    check(scene.undo(), "Coordinated source5 Apply Undo was missing");
    check(
        scene.entity(first).has<NavigationSurface>() &&
            !scene.entity(second).has<NavigationSurface>() &&
            project.prefabs().source(variant).at("overrides") == suppressed.at("overrides") &&
            project.prefabs().source(variant).at("revision").get<std::uint64_t>() >
                applied_revision,
        "Undo lost the instance's pre-Apply intent or failed to restore suppression monotonically");
    check(scene.redo() && scene.entity(second).has<NavigationSurface>(),
          "Coordinated source5 Apply Redo did not restore component inheritance");
    const auto snapshot = scene.snapshot();
    const auto current = project.prefabs().source(variant);
    auto incompatible = current;
    incompatible["base"] = AssetId::generate();
    rejects([&] {
        project.prefabs().publish(scene, current, incompatible,
                                  project.prefabs().base_sources(scene, current));
    });
    check(scene.snapshot() == snapshot && project.prefabs().source(variant) == current,
          "Rejected base-identity replacement changed durable/native state");
    // Simulate an external source update after Apply; paired Undo must retain
    // both files, the current Scene and the unconsumed history entry.
    auto external = current;
    external["revision"] = current.at("revision").get<std::uint64_t>() + 1;
    atomic_write(root / "Assets/Variant.prefab.json", external.dump(2));
    rejects([&] { scene.undo(); });
    check(scene.can_undo() && scene.snapshot() == snapshot &&
              project.prefabs().source(variant) == external &&
              project.prefabs().source(base) == saved_base,
          "Stale coordinated Undo consumed history or overwrote external source5 data");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide a scratch directory");
        exercise(std::filesystem::absolute(argv[1]) / AssetId::generate().str());
        std::cout << "Source5 publication, paired Apply/history and conflicts passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
