#include "document.hpp"
#include "native_build.hpp"
#include <iostream>
#include <stdexcept>

void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), {}};
}
std::string replace(std::string text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    require(position != std::string::npos, "Test source marker missing");
    text.replace(position, from.size(), to);
    return text;
}
int main(int argc, char** argv) {
    if (argc != 5)
        return 2;
    const auto root = std::filesystem::current_path() /
                      ("native editor test " + std::to_string(SDL_GetPerformanceCounter()));
    forge::SceneDocument::create_project(root, "Native workflow test");
    int result = 0;
    try {
        forge::EngineContext authored_engine;
        forge::Scene authored(authored_engine.world());
        auto scene = authored.document();
        scene["entities"].push_back(
            {{"id", forge::EntityId::generate().str()},
             {"name", "Test"},
             {"components", {{"forge.local_translation", {{"x", 5}, {"y", 123}, {"z", 0}}}}}});
        authored.replace(scene);
        forge::PlaySession play;
        forge::NativeBuild native(root, argv[1], argv[2]);
        native.cmake = argv[3];
        native.ninja = argv[4];
        native.create_source();
        const auto source = root / "Native/gameplay.cpp";
        const auto original = read(source);
        try {
            native.create_source();
            throw std::logic_error("Existing source was overwritten");
        } catch (const std::runtime_error&) {
        }
        require(read(source) == original, "Create source modified existing gameplay");
        auto pump = [&] {
            play.pump();
            native.pump(play, authored.document());
            SDL_Delay(1);
        };
        auto settle = [&] {
            const auto limit = SDL_GetTicks() + 45000;
            while ((native.busy() || (play.active() && !play.ready())) && SDL_GetTicks() < limit)
                pump();
            require(!native.busy(), "Build did not finish: " + native.status());
        };
        auto step_for = [&](Uint64 milliseconds) {
            const auto limit = SDL_GetTicks() + milliseconds;
            while (SDL_GetTicks() < limit)
                pump();
        };
        auto x = [&] {
            return play.snapshot()["entities"][0]["components"]["forge.local_translation"]["x"]
                .get<float>();
        };
        native.build();
        settle();
        require(!native.artifact().empty(), "Initial build failed: " + native.log());
        play.start(argv[2], scene, native.artifact());
        settle();
        step_for(100);
        require(play.ready() && x() > 5, "Compiled gameplay did not execute");
        const auto first = native.artifact();
        forge::atomic_write(source, "invalid C++ source");
        native.build();
        settle();
        require(native.artifact() == first && play.ready(),
                "Failed build replaced or stopped gameplay");
        auto before = x();
        step_for(100);
        require(x() > before, "Old module stopped after compile failure");
        // A compatible change reverses movement without resetting the play state.
        forge::atomic_write(source, replace(original, "seconds, 0.0f", "-seconds, 0.0f"));
        native.build();
        settle();
        require(native.artifact() != first && play.module() == native.artifact(),
                "Compatible reload failed: " + native.log());
        before = x();
        step_for(100);
        require(x() < before, "Replacement gameplay is not executing");
        const auto compatible = native.artifact();
        // A probe crash must not touch the running process.
        forge::atomic_write(
            source,
            replace(
                original, "static void tick(const ForgeHostV1* host, float seconds) {",
                "static void tick(const ForgeHostV1* host, float seconds) { *(volatile int*)0=1;"));
        native.build();
        settle();
        require(native.artifact() == compatible && play.ready(),
                "Probe failure changed active module");
        // Pass the probe, crash on the second entry call in the actual play process.
        const auto marker = forge::Json((root / "activation-marker").string()).dump();
        auto activation = "#include <fstream>\n#include <cstdlib>\n" + original;
        activation =
            replace(activation, "FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) {",
                    "FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) { if (std::ifstream(" +
                        marker + ").good()) std::abort(); std::ofstream(" + marker + ") << 1;");
        forge::atomic_write(source, activation);
        native.build();
        settle();
        require(native.artifact() == compatible && play.ready() && play.module() == compatible,
                "Activation crash did not restore previous module: " + play.status());
        before = x();
        step_for(100);
        require(x() < before, "Recovered previous module is not running");
        // Schema changes explicitly restart the world, preserving supported host-owned values.
        forge::atomic_write(source, replace(original, "forge.position.v1", "forge.position.v2"));
        native.build();
        settle();
        require(native.artifact() != compatible && play.ready(),
                "Schema restart failed: " + native.log());
        require(play.status().find("Schema changed") != std::string::npos,
                "Schema restart was not reported");
        require(play.snapshot()["entities"][0]["components"]["forge.local_translation"]["y"] == 123,
                "Reflected state lost across reload");
        // Source watching detects a new save and deploys it without pressing Build.
        const auto schema_artifact = native.artifact();
        native.auto_build = true;
        forge::atomic_write(source, original);
        const auto watch_limit = SDL_GetTicks() + 45000;
        while (native.artifact() == schema_artifact && SDL_GetTicks() < watch_limit)
            pump();
        require(native.artifact() != schema_artifact, "Build on save failed: " + native.log());
        native.auto_build = false;
        // A later gameplay crash exposes manual checkpoint recovery without a crash loop.
        const auto tick_marker = forge::Json((root / "tick-marker").string()).dump();
        auto crash_once = "#include <fstream>\n#include <cstdlib>\n" + original;
        crash_once =
            replace(crash_once, "static void tick(const ForgeHostV1* host, float seconds) {",
                    "static void tick(const ForgeHostV1* host, float seconds) { static int "
                    "calls=0; if (++calls == 4 && "
                    "!std::ifstream(" +
                        tick_marker + ").good()) { { std::ofstream marker(" + tick_marker +
                        "); marker << 1; } std::abort(); }");
        forge::atomic_write(source, crash_once);
        native.build();
        settle();
        const auto crash_limit = SDL_GetTicks() + 6500;
        while (play.active() && SDL_GetTicks() < crash_limit)
            pump();
        require(play.can_recover(), "Runtime crash did not offer checkpoint recovery: " +
                                        play.status() + " | " + native.status());
        const auto checkpoint = play.snapshot();
        play.recover();
        settle();
        require(play.ready(), "Checkpoint recovery failed");
        require(play.snapshot()["entities"][0]["components"]["forge.local_translation"]["y"] ==
                    checkpoint["entities"][0]["components"]["forge.local_translation"]["y"],
                "Recovery lost reflected state");
        before = x();
        step_for(100);
        require(x() > before, "Recovered gameplay is not executing");
        auto wait_until = [&](auto condition, const char* message) {
            const auto limit = SDL_GetTicks() + 6000;
            do {
                pump();
            } while (!condition() && SDL_GetTicks() < limit);
            require(condition(),
                    std::string(message) + ": " + play.status() + " | " + native.status());
        };
        play.pause();
        wait_until([&] { return play.ready() && play.paused(); }, "Pause failed");
        const auto paused_tick = play.timing().at("tick").get<std::uint64_t>();
        const auto paused_x = x();
        step_for(80);
        require(play.timing().at("tick") == paused_tick && x() == paused_x,
                "Paused simulation drifted");
        const auto known_good = native.artifact();
        forge::atomic_write(source, original);
        native.build();
        settle();
        require(play.pending_activation() && native.artifact() == known_good &&
                    play.module() == known_good,
                "Paused load reported active before first tick");
        require(play.timing().at("tick") == paused_tick && x() == paused_x,
                "Paused reload advanced state");
        play.step();
        wait_until([&] { return !play.pending_activation() && native.artifact() != known_good; },
                   "Step activation failed");
        require(play.paused() && play.timing().at("tick") == paused_tick + 1,
                "Step activation was not exactly one tick");
        const auto stepped = native.artifact();
        forge::atomic_write(source, replace(original, "seconds, 0.0f", "2 * seconds, 0.0f"));
        native.build();
        settle();
        require(play.pending_activation(), "Expected pending candidate");
        // A newer candidate supersedes the unvalidated one from the original checkpoint.
        const auto superseded_session = play.session();
        forge::atomic_write(source, replace(original, "seconds, 0.0f", "3 * seconds, 0.0f"));
        native.build();
        settle();
        require(play.pending_activation() && native.artifact() == stepped &&
                    play.session() != superseded_session,
                "Pending activation was stacked rather than superseded");
        play.resume();
        wait_until(
            [&] {
                return !play.pending_activation() && !play.paused() && native.artifact() != stepped;
            },
            "Resume activation failed");
        play.pause();
        wait_until([&] { return play.paused(); }, "Pause after resume failed");
        const auto fallback = native.artifact();
        const auto fallback_x = x();
        const auto fallback_session = play.session();
        const auto fallback_tick = play.timing().at("tick");
        // Probe tick passes; the first LIVE callback fails, using explicit process state.
        const auto live_marker = forge::Json((root / "first-live-marker").string()).dump();
        auto first_live =
            "#include <fstream>\n#include <cstdlib>\nstatic bool live_process=false;\n" + original;
        first_live =
            replace(first_live, "FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) {",
                    "FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) { "
                    "live_process=std::ifstream(" +
                        live_marker + ").good(); std::ofstream(" + live_marker + ") << 1;");
        first_live =
            replace(first_live, "static void tick(const ForgeHostV1* host, float seconds) {",
                    "static void tick(const ForgeHostV1* host, float seconds) { if(live_process) "
                    "std::abort();");
        forge::atomic_write(source, first_live);
        native.build();
        settle();
        require(play.pending_activation(), "Probe unexpectedly rejected live-only failure");
        play.step();
        wait_until(
            [&] {
                return play.ready() && play.reload_result() == forge::PlaySession::Reload::Failed &&
                       !native.busy();
            },
            "First live tick did not recover");
        require(play.paused() && native.artifact() == fallback && play.module() == fallback &&
                    x() == fallback_x,
                "First-tick rollback lost previous artifact/checkpoint or paused policy");
        require(play.session() != fallback_session && play.timing().at("tick") == fallback_tick &&
                    play.timing().at("alpha") == 1,
                "Recovery did not reset timing/generation");
        // Stop cancels a loaded pending candidate without ever invoking gameplay.
        forge::atomic_write(source, original);
        native.build();
        settle();
        require(play.pending_activation(), "Stop test requires pending candidate");
        play.stop();
        pump();
        require(!play.active() && native.artifact() == fallback && !native.busy(),
                "Stop did not cancel pending activation");
        require(authored.document() == scene, "Gameplay changed authored scene");
        require(std::filesystem::is_regular_file(first), "Previous build artifact was removed");
        std::cout << "Editor native build, reload, rollback, watch and source isolation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(root);
    return result;
}
