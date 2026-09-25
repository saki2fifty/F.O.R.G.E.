#include "../src/authored_inspection.hpp"
#include "play.hpp"
#include <forge/authoring.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <iostream>
#include <type_traits>

namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
// Static assertions on the centralized helpers so the contract is
// pinned in the test build, not just implied by source review.
static_assert(std::is_same<decltype(std::declval<forge::PlaySession&>().current_effective_epoch()),
                           std::uint64_t>::value,
              "current_effective_epoch must return uint64");
static_assert(std::is_same<decltype(std::declval<forge::PlaySession&>().next_editor_epoch()),
                           std::uint64_t>::value,
              "next_editor_epoch must return uint64");
} // namespace
int main(int argc, char** argv) {
    if (argc != 6)
        return 2;
    const auto root =
        std::filesystem::current_path() / ("sdk-editor-" + forge::AssetId::generate().str());
    int result = 0;
    try {
        std::filesystem::create_directories(root);
        forge::EngineContext engine;
        forge::Scene authored(engine.world());
        auto original = authored.snapshot();
        auto settings = forge::ProjectSettings::defaults("SDK editor test");
        auto install = [&](const char* path) {
            const auto library = std::filesystem::path(path).filename();
            std::filesystem::copy_file(path, root / library,
                                       std::filesystem::copy_options::overwrite_existing);
            settings["modules"] =
                forge::Json::array({{{"id", "project.sdk_probe"},
                                     {"implementation", "1"},
                                     {"sdk", "experimental-1"},
                                     {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                                     {"library", library.string()},
                                     {"dependencies", {"forge.input", "forge.transforms"}}}});
            forge::atomic_write(root / "forge.project.json", settings.dump());
        };
        forge::PlaySession play;
        play.configure(60, forge::InputMap{}, {0, -9.81, 0}, root, true);
        auto wait = [&](auto done) {
            const auto limit = SDL_GetTicks() + 10000;
            do {
                play.pump();
                SDL_Delay(1);
            } while (!done() && SDL_GetTicks() < limit);
            require(done(), "SDK controller timeout: " + play.status() + " " + play.log());
        };
        install(argv[2]);
        play.start(argv[5], original, {}, true);
        wait([&] { return !play.active(); });
        require(!play.can_recover() &&
                    play.status().find("SDK runtime does not match") != std::string::npos,
                "An incompatible runtime was accepted for SDK Editor Play: " + play.status());
        const auto inspected = forge::detail::inspect_project_authoring(
            std::filesystem::absolute(argv[1]), root, FORGE_NATIVE_SDK_FINGERPRINT);
        authored.publish_component_schemas(inspected.at("components"));
        const std::string entity =
            forge::authoring_command(authored, "entity.create").at("selected");
        forge::authoring_command(authored, "component.add",
                                 {{"entity", entity}, {"component", "project.health"}});
        const forge::Json edited_values{{"health", 73.0}, {"lives", UINT64_MAX}};
        for (const auto& [field, value] : edited_values.items())
            forge::authoring_command(authored, "property.set",
                                     {{"entity", entity},
                                      {"component", "project.health"},
                                      {"field", field},
                                      {"value", value}});
        original = authored.snapshot();
        authored.save(root / "authored.scene.json");
        authored.load(root / "authored.scene.json");
        require(authored.snapshot() == original,
                "Authored SDK values changed during scene save/reopen");
        play.start(argv[1], original, {}, true);
        wait([&] { return !play.active() || play.ready(); });
        require(play.ready() && play.paused(), "SDK Editor Play failed: " + play.status());
        const auto session = play.session();
        play.step();
        wait([&] { return play.timing().value("tick", 0) == 1; });
        require(play.log().find("SDK fixed tick") != std::string::npos,
                "Project SDK system did not execute through Editor Play");
        require(play.log().find("SDK authored health 73 lives 18446744073709551615") !=
                    std::string::npos,
                "Matching SDK runtime did not consume persisted authoring values exactly: " +
                    play.log());
        require(authored.snapshot() == original, "SDK Play modified authoring");
        try {
            play.reload("unused");
            throw std::logic_error("Rich SDK in-place reload was accepted");
        } catch (const std::runtime_error&) {
        }
        play.stop();
        install(argv[3]);
        play.start(argv[1], original, {}, true);
        wait([&] { return !play.active(); });
        require(!play.can_recover(), "Rejected SDK offered partial recovery");
        install(argv[2]);
        play.start(argv[1], original, {}, true);
        wait([&] { return !play.active() || play.ready(); });
        require(play.ready() && play.session() != session && play.timing().at("tick") == 0,
                "SDK restart did not create a fresh paused world");
        play.stop();
        install(argv[4]);
        play.start(argv[1], original, {}, true);
        wait([&] { return !play.active() || play.ready(); });
        require(play.ready(), "Crash fixture did not reach Play");
        play.step();
        wait([&] { return !play.active(); });
        require(!play.can_recover() && authored.snapshot() == original,
                "SDK crash offered incomplete custom-state recovery or changed authoring");
        std::cout << "SDK Editor Play: registration, fixed Step, restart, rejection and crash "
                     "isolation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(root);
    return result;
}
