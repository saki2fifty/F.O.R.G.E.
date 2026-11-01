#include "application_state_authoring.hpp"
#include "application_state_resource.hpp"
#include "asset_file_operations.hpp"
#include "asset_reference_impact.hpp"
#include "runtime_package.hpp"
#include <forge/application_states.hpp>
#include <forge/asset_discovery.hpp>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <iostream>

namespace {
using namespace forge;
using namespace std::chrono_literals;
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid application state operation accepted");
}
std::uint64_t ready(GameSession& session, const Json& snapshot) {
    const auto ticket = session.prepare(snapshot);
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!session.poll_preparation(ticket).ready) {
        check(std::chrono::steady_clock::now() < deadline, "State resource preparation timed out");
        std::this_thread::sleep_for(2ms);
    }
    session.activate(ticket, RuntimeClock::Time{}, false);
    return ticket;
}
void pipeline(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets");
    atomic_write(root / "forge.assets.json",
                 Json{{"version", 1}, {"assets", Json::array()}}.dump());
    const auto writer = std::make_shared<ProjectLease>(root);
    auto document = ApplicationStatesDocument::create(writer, "Assets/Modes.appstates.json");
    const auto asset = document->source().asset();
    const auto original = document->source().document;
    const auto revision = document->revision();
    document->edit(revision, "Describe game mode", [&](Json& source) {
        source["domains"][0]["label"] = asset.str();
        source["extension"] = {{"future", "Preserved"}};
    });
    const auto edited = document->source().document;
    rejects([&] { document->edit(revision, "Stale", [](Json&) {}); });
    rejects([&] {
        document->edit(document->revision(), "Invalid initial",
                       [](Json& source) { source["domains"][0]["initial"] = "missing"; });
    });
    rejects([&] {
        document->edit(document->revision(), "Identity",
                       [](Json& source) { source["asset_id"] = AssetId::generate(); });
    });
    check(document->source().document == edited, "Rejected source edit changed state/history");
    document->undo();
    check(document->source().document == original, "Source Undo did not restore initial values");
    document->redo();
    check(document->source().document == edited, "Source Redo lost opaque data");
    document->save();
    AssetImportService imports(writer, application_states_import_registry(),
                               {"windows", "d3d12", "desktop"}, 1);
    const auto publish = [&] {
        imports.submit(
            imports.prepare(document->locator(), {}, asset),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_application_states_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(10s), "State publication timed out");
        const auto results = imports.poll();
        check(results.size() == 1, "Missing state publication result");
        return results.front();
    };
    auto result = publish();
    check(result.published, result.diagnostic);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.records().at(asset);
    const auto key = record.metadata.at("forge.import").at("key").get<std::string>();
    DerivedDataCache cache(root);
    const auto artifact = cache.load_selected(key, [](const auto&) {});
    const auto definition = admit_application_states_artifact(record, artifact);
    check(definition.domains[0].label == asset.str() && definition.domains[0].initial == "menu",
          "Admitted definition lost source values");
    auto wrong = record;
    wrong.metadata["forge.import"]["settings_digest"] = std::string(64, '0');
    rejects([&] { admit_application_states_artifact(wrong, artifact); });
    wrong = record;
    wrong.metadata["forge.application_states"]["bytes"] = 1;
    rejects([&] { admit_application_states_artifact(wrong, artifact); });
    auto swapped = artifact;
    swapped.key = std::string(64, '0');
    rejects([&] { admit_application_states_artifact(record, swapped); });
    const auto copied_id = AssetId::generate();
    const auto copied = Json::parse(rewrite_authored_asset(
        record, edited.dump(), "Assets/Copy.appstates.json", {{asset, copied_id}}));
    check(copied.at("asset_id").get<AssetId>() == copied_id &&
              copied.at("domains") == edited.at("domains") &&
              copied.at("extension") == edited.at("extension"),
          "Source duplication changed semantic keys/labels/extensions");
    rejects([&] {
        rewrite_authored_asset(record, edited.dump(), "Assets/Wrong.json", {{asset, copied_id}});
    });
    auto recognized = edited;
    recognized.erase("extension");
    const std::array reference_documents{AssetReferenceDocument{document->locator(), recognized}};
    const auto references =
        collect_asset_references({{"components", Json::array()}}, reference_documents);
    check(references.references.empty() && references.uninspected.empty(),
          "Semantic labels treated as asset references");
    const auto scan = scan_asset_sources(root);
    check(scan.complete &&
              scan.files.at(document->locator()).source_kind == ApplicationStatesAsset::type,
          "Content discovery did not recognize state sources");

    WorldContext authored;
    Scene scene(authored);
    scene.replace({{"version", 1},
                   {"entities", Json::array({{{"id", "modes"},
                                              {"name", "Game modes"},
                                              {"components", Json::object()}}})}});
    bool admitted = false;
    const auto component_schema = scene.schema();
    for (const auto& type : component_schema.at("components"))
        if (type.at("id") == "forge.application_states")
            admitted = type.at("optional").get<bool>() && type.at("category") == "Gameplay";
    check(admitted, "Application States is absent from the shared Add Component metadata");
    const auto before_attach = scene.snapshot();
    apply_authoring(
        scene,
        Json::array(
            {{{"operation", "component.add"},
              {"arguments", {{"entity", "modes"}, {"component", "forge.application_states"}}}},
             {{"operation", "property.set"},
              {"arguments",
               {{"entity", "modes"},
                {"component", "forge.application_states"},
                {"field", "definition"},
                {"value", asset}}}}}),
        scene.revision());
    const auto snapshot = scene.snapshot();
    scene.undo();
    check(scene.snapshot() == before_attach,
          "Shared controller creation was not one Scene Undo step");
    scene.redo();
    check(scene.snapshot() == snapshot,
          "Shared controller creation Redo changed its typed reference");
    check(scene.entity("modes").get<ApplicationStates>().definition.id == asset,
          "Reflected controller did not retain its typed asset reference");
    authoring_command(scene, "property.set",
                      {{"entity", "modes"},
                       {"component", "forge.application_states"},
                       {"field", "definition"},
                       {"value", nullptr}});
    check(!scene.entity("modes").get<ApplicationStates>().definition.id,
          "Controller property edit did not apply");
    scene.undo();
    check(scene.snapshot() == snapshot, "Scene Undo changed the state definition reference");
    GameSessionConfig config;
    config.content_root = root;
    GameSession live(config);
    ready(live, snapshot);
    check(observe_application_states(live.active().engine.world().world()).at(0).active == "menu",
          "Authored definition did not initialize before candidate activation");
    auto duplicate = snapshot;
    duplicate["entities"].push_back(duplicate["entities"][0]);
    duplicate["entities"][1]["id"] = EntityId::generate();
    rejects([&] { live.prepare(duplicate); });
    check(observe_application_states(live.active().engine.world().world()).at(0).active == "menu",
          "Rejected candidate disturbed the active state");
    request_application_state(live.active().engine.world().world(), "app", "playing");
    live.control_frame();
    check(live.active().scene.snapshot() == snapshot,
          "Runtime state leaked into authored scene storage");

    document->edit(document->revision(), "New initial mode",
                   [](Json& source) { source["domains"][0]["initial"] = "game_over"; });
    document->save();
    result = publish();
    check(result.published, result.diagnostic);
    catalog = AssetCatalog::open_project(root);
    const auto current = catalog.records().at(asset);
    check(current.id == record.id && current.metadata.at("forge.import").at("key") != key &&
              definition.domains[0].initial == "menu",
          "Publication changed identity or mutated admitted predecessor");
    check(observe_application_states(live.active().engine.world().world()).at(0).active ==
              "playing",
          "Asset publication mutated the active runtime definition");
    const auto saved = document->source().document.dump();
    atomic_write(root / document->locator(), "{}");
    bool rejected = false;
    try {
        rejected = !publish().published;
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Malformed source publication succeeded");
    check(AssetCatalog::open_project(root).records().at(asset).metadata == current.metadata,
          "Failed source admission replaced the last-good selection");
    atomic_write(root / document->locator(), saved);
    const std::array roots{asset};
    const auto package = root.parent_path() / (asset.str() + "-export");
    package_runtime_content(root, package, roots, {"windows", "d3d12"});
    const auto moved = root.parent_path() / (asset.str() + " Relocated content");
    std::filesystem::rename(package, moved);
    check(!std::filesystem::exists(moved / document->locator()),
          "Runtime package retained editable state source");
    const auto hidden = root / "Hidden source";
    std::filesystem::rename(root / "Assets", hidden);
    GameSessionConfig exported_config;
    exported_config.content_root = moved;
    GameSession exported(exported_config);
    ready(exported, snapshot);
    check(observe_application_states(exported.active().engine.world().world()).at(0).active ==
              "game_over",
          "Relocated source-free runtime did not load its selected definition");
    request_application_state(exported.active().engine.world().world(), "app", "playing");
    exported.step();
    check(observe_application_states(exported.active().engine.world().world()).at(0).active ==
              "playing",
          "Exported state did not transition through the existing runtime boundary");
    exported.unload(RuntimeClock::Time{});
    live.unload(RuntimeClock::Time{});
    document.reset();
    std::filesystem::remove_all(moved);
    std::filesystem::remove_all(root);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need state pipeline scratch root");
        pipeline(std::filesystem::absolute(argv[1]) / AssetId::generate().str());
        std::cout
            << "Application states: source/scene history, publication, provenance, typed runtime "
               "loading, last-good selection and source-free relocated content passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
