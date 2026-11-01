#pragma once
#include "application_state_authoring.hpp"
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/scene.hpp>

namespace forge::test {
inline Json application_state_fixture(const std::filesystem::path& root) {
    using namespace std::chrono_literals;
    const auto check = [](bool value, const std::string& message) {
        if (!value)
            throw std::runtime_error(message);
    };
    std::filesystem::create_directories(root / "Assets");
    auto settings = ProjectSettings::defaults("State controls");
    settings["game"] =
        default_game_settings("org.forge.states-" + AssetId::generate().str(), "States");
    atomic_write(root / "forge.project.json", settings.dump(2));
    if (!std::filesystem::exists(root / "forge.assets.json"))
        atomic_write(root / "forge.assets.json",
                     Json{{"version", 1}, {"assets", Json::array()}}.dump());
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = ApplicationStatesDocument::create(lease, "Assets/Modes.appstates.json");
    document->edit(document->revision(), "Computed status", [](Json& source) {
        source["domains"][0]["states"].push_back({{"key", "game"}});
        source["domains"][0]["states"][1]["parent"] = "game";
        source["domains"].push_back(
            {{"key", "hud"},
             {"initial", "hidden"},
             {"computed", true},
             {"states", Json::array({{{"key", "hidden"}}, {{"key", "visible"}}})},
             {"rules",
              Json::array({{{"state", "visible"},
                            {"all", Json::array({{{"domain", "app"}, {"state", "playing"}}})}}})}});
        source["domains"].push_back(
            {{"key", "level"},
             {"initial", "warmup"},
             {"enabled_when", {{"domain", "app"}, {"state", "game"}}},
             {"states", Json::array({{{"key", "warmup"}}, {{"key", "round"}}})}});
    });
    document->save();
    AssetImportService imports(lease, application_states_import_registry(),
                               {"windows", "d3d12", "desktop"}, 1);
    imports.submit(
        imports.prepare(document->locator(), {}, document->source().asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_application_states_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(10s), "State import timed out");
    const auto results = imports.poll();
    check(results.size() == 1 && results.front().published,
          results.empty() ? "No import result" : results.front().diagnostic);
    WorldContext world;
    Scene scene(world);
    scene.replace(
        {{"version", 1},
         {"entities", Json::array({{{"id", "modes"},
                                    {"name", "Modes"},
                                    {"components",
                                     {{"forge.application_states",
                                       {{"definition", document->source().asset()}}}}}}})}});
    const auto authored = scene.snapshot();
    atomic_write(root / "main.scene.json", authored.dump(2));
    settings["startup_scene"] = {{"asset", authored.at("asset_id")}, {"source", "main.scene.json"}};
    atomic_write(root / "forge.project.json", settings.dump(2));
    return authored;
}
} // namespace forge::test
