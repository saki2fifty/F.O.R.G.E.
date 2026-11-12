#include "ui_localization_fixture.hpp"
#include <forge/authoring.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_assets.hpp>
#include <forge/ui_values.hpp>
#include <iostream>

using namespace forge;
using namespace std::chrono_literals;
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
    throw std::runtime_error("Invalid UI locale operation was accepted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need UI localization runtime scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        const auto hud = create_ui_example(root);
        {
            const auto lease = std::make_shared<ProjectLease>(root);
            const auto first =
                test_ui_localization::publish(lease, "Assets/First.localization.json");
            const auto second =
                test_ui_localization::publish(lease, "Assets/Second.localization.json");
            EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
            Scene scene(engine.world());
            Json entities = Json::array();
            for (const auto* name : {"hud", "language", "other"})
                entities.push_back({{"id", name}, {"name", name}, {"components", Json::object()}});
            scene.restore_snapshot({{"version", 1}, {"entities", entities}});
            scene.entity("hud").set<UiDocument>({{hud.id}, true, true, 0});
            scene.entity("language").set<UiLocalization>({{first}, "en"});
            auto service = std::static_pointer_cast<UiRuntime>(engine.services().ui());
            auto state = service->snapshot(scene, "locale-test", 1, 77, true);
            check(state.at("localization").at("asset") == Json(first) &&
                      state.at("localization").at("locale") == "en" &&
                      state.at("documents").at(0).at("commands").size() == 5,
                  "UI snapshot omitted the single Scene controller or locale capability");
            auto instance = state.at("documents").at(0).at("instance").get<std::string>();
            unsigned controls = 0;
            const auto command = [&](const std::string& target, const Json& value) {
                service->command(
                    scene, {{"instance", target}, {"command", "forge.locale"}, {"value", value}},
                    [&](const auto&) { ++controls; });
            };
            command(instance, "FR-ca");
            check(scene.entity("language").get<UiLocalization>().locale == "FR-ca" && controls == 0,
                  "Locale command changed another owner or advanced a host clock control");
            state = service->snapshot(scene, "locale-test", 1, 77, true);
            check(state.at("localization").at("locale") == "FR-ca" &&
                      state.at("documents").at(0).at("instance") == instance,
                  "Locale-only change replaced the document incarnation");
            rejects([&] { command(instance, "fr_CA"); });
            rejects([&] { command(instance, 12); });
            check(scene.entity("language").get<UiLocalization>().locale == "FR-ca",
                  "Rejected locale input mutated live configuration");
            rejects([&] {
                scene.tune_runtime_property(scene.reference("language").entity.str(),
                                            "forge.ui_localization", "catalog", second);
            });
            (void)scene.tune_runtime_property(scene.reference("language").entity.str(),
                                              "forge.ui_localization", "locale", "fr");
            check(scene.entity("language").get<UiLocalization>().locale == "fr",
                  "Live tuning did not use the same locale component");
            auto config = scene.entity("language").get<UiLocalization>();
            config.catalog.id = second;
            scene.entity("language").set(config);
            rejects([&] { command(instance, "de"); });
            state = service->snapshot(scene, "locale-test", 1, 77, true);
            const auto changed = state.at("documents").at(0).at("instance").get<std::string>();
            check(changed != instance, "Changed catalog did not renew UI input authority");
            rejects([&] { command(instance, "de"); });
            command(changed, "de");
            scene.entity("other").set<UiLocalization>({{first}, "en"});
            rejects([&] { (void)service->snapshot(scene, "locale-test", 1, 77, true); });
            rejects([&] { command(changed, "it"); });
            scene.entity("language").remove<UiLocalization>();
            state = service->snapshot(scene, "locale-test", 1, 77, true);
            check(state.at("localization").at("entity") == Json(scene.reference("other").entity) &&
                      state.at("documents").at(0).at("instance") != changed,
                  "New controller retained the old controller's command authority");

            EngineContext independent(WorldRole::Runtime, false, {ui_module(root)});
            Scene copy(independent.world());
            copy.restore_snapshot(scene.snapshot());
            copy.entity("other").set<UiLocalization>({{first}, "es"});
            auto other_ui = std::static_pointer_cast<UiRuntime>(independent.services().ui());
            check(other_ui->snapshot(copy, "independent", 1, 77, true)
                              .at("localization")
                              .at("locale") == "es" &&
                      scene.entity("other").get<UiLocalization>().locale == "en",
                  "Independent Worlds shared locale state");

            for (unsigned i = 0; i < 29; ++i)
                service->allow_action("custom_" + std::to_string(i));
            state = service->snapshot(scene, "locale-test", 1, 77, true);
            check(state.at("documents").at(0).at("commands").size() == 34,
                  "Localized UI lost an existing custom command slot");
            auto legacy = state;
            legacy.erase("localization");
            rejects([&] { ui_protocol::validate_snapshot(legacy); });
            scene.entity("hud").set<UiValues>({{{"forge_locale", .5}}});
            rejects([&] { (void)service->snapshot(scene, "locale-test", 1, 77, true); });
            scene.entity("hud").remove<UiValues>();
            service->publish(scene.reference("hud").entity, "forge_locale", "custom value");
            rejects([&] { (void)service->snapshot(scene, "locale-test", 1, 77, true); });

            other_ui->allow_value_action("forge.locale");
            rejects([&] { (void)other_ui->snapshot(copy, "independent", 1, 77, true); });
            copy.entity("other").remove<UiLocalization>();
            auto ordinary = other_ui->snapshot(copy, "independent", 1, 77, true);
            const auto ordinary_instance = ordinary.at("documents").at(0).at("instance");
            other_ui->command(copy,
                              {{"instance", ordinary_instance},
                               {"command", "forge.locale"},
                               {"value", "existing custom action"}},
                              [](const auto&) {});
            check(other_ui->poll_action("forge.locale").has_value(),
                  "Unlocalized project lost its preexisting custom action");
            check(!ordinary.contains("localization"), "Legacy UI gained a localization descriptor");
            {
                EngineContext prefab_world(WorldRole::Runtime, false, {ui_module(root)});
                Scene inherited(prefab_world.world());
                inherited.restore_snapshot({{"version", 1}, {"entities", entities}});
                inherited.entity("hud").set<UiDocument>({{hud.id}, true, true, 0});
                auto base = inherited.entity("language");
                base.add(flecs::Prefab).set<UiLocalization>({{first}, "en-Latn-GB-variant1"});
                inherited.entity("other").is_a(base);
                auto inherited_ui =
                    std::static_pointer_cast<UiRuntime>(prefab_world.services().ui());
                const auto before = inherited_ui->snapshot(inherited, "prefab-locale", 1, 0, true);
                check(before.at("localization").at("entity") ==
                          Json(inherited.reference("other").entity),
                      "Prefab template was counted as a second configured controller");
                const auto serialized = inherited.snapshot();
                inherited.restore_snapshot(serialized);
                check(inherited.entity("other").get<UiLocalization>().locale ==
                          "en-Latn-GB-variant1",
                      "Scene round-trip lost an inherited owning locale string");
                (void)inherited.tune_runtime_property(inherited.reference("other").entity.str(),
                                                      "forge.ui_localization", "locale", "es");
                check(
                    inherited.entity("other").get<UiLocalization>().locale == "es" &&
                        inherited.entity("language").get<UiLocalization>().locale ==
                            "en-Latn-GB-variant1" &&
                        inherited.entity("other").get<UiLocalization>().catalog.id == first,
                    "Live instance override changed template text or its typed catalog reference");
            }
        } // Release project writers and native/resource owners before deleting the source.
        std::filesystem::remove_all(root);
        std::cout << "Scene locale, copied protocol, paused controls, scope renewal and legacy UI "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
