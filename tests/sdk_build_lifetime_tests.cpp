#include "editor/sdk_build.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void put(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    check(bool(output << text) && bool(output.flush()), "Cannot write SDK lifetime fixture");
}
int compiler(int argc, char** argv) {
    std::filesystem::path build;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "-B")
            build = std::filesystem::u8path(argv[i + 1]);
    const auto* variable = std::getenv("FORGE_SDK_LIFETIME_FIXTURE");
    check(variable && !build.empty(), "Missing SDK lifetime fixture parameters");
    const auto control = std::filesystem::u8path(variable);
    const auto mode = read_json(control / "mode.json").get<std::string>();
    put(build / "compiler-output.txt", "retain these compiler bytes");
    put(control / "observed.json", Json{{"build", path_utf8(build)}}.dump());
    if (mode == "hold")
        std::this_thread::sleep_for(std::chrono::seconds(5));
#ifdef __linux__
    if (mode == "crash") {
        // This fixture's direct parent is the isolated helper. Parent death also
        // kills this direct target; the editor must still require a receipt.
        check(kill(getppid(), SIGKILL) == 0, "Cannot inject helper failure");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
#endif
    return 7;
}
void finish(SdkBuild& build, SceneDocument& document) {
    const auto deadline = SDL_GetTicks() + 8000;
    while (build.busy()) {
        build.pump(document);
        check(SDL_GetTicks() < deadline, "SDK task did not settle after compiler retirement");
        SDL_Delay(1);
    }
}
std::filesystem::path observed(const std::filesystem::path& control) {
    return std::filesystem::u8path(
        read_json(control / "observed.json").at("build").get<std::string>());
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "-S")
            return compiler(argc, argv);
        check(argc == 2, "Need SDK lifetime scratch parent");
        check(SDL_Init(0), "Cannot initialize SDK lifetime fixture");
        struct SdlCleanup {
            ~SdlCleanup() { SDL_Quit(); }
        } sdl_cleanup;
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
#ifdef __linux__
        check(prctl(PR_SET_CHILD_SUBREAPER, 1L, 0L, 0L, 0L) == 0,
              "Cannot own fixture orphans after injected helper crash");
        struct ReapFixture {
            ~ReapFixture() {
                while (waitpid(-1, nullptr, 0) >= 0 || errno == EINTR) {
                }
            }
        } reap_fixture;
#endif
        const auto project = root / "Project", sdk = root / "SDK", control = root / "Control";
        std::filesystem::create_directory(control);
        check(SDL_setenv_unsafe("FORGE_SDK_LIFETIME_FIXTURE", path_utf8(control).c_str(), 1) == 0,
              "Cannot set SDK fixture control path");
#ifdef _WIN32
        check(SDL_setenv_unsafe("VCToolsInstallDir", "fixture", 1) == 0,
              "Cannot select fixture compiler environment");
#endif
        put(sdk / "build.json",
            Json{{"linkage_profile", "shared-native-sdk"}, {"source_commit", forge::source_commit}}
                .dump());
        put(sdk / "sdk/ForgeNativeSdkConfig.cmake", "fixture configuration");
        put(SdkBuild::runtime_path(sdk), "fixture runtime (never executed)");
        for (const auto* name : {"CMakeLists.txt", "gameplay.cpp", "forge.registration.hpp"})
            put(sdk / "sdk/template" / name, "fixture source\n");
        SceneDocument::create_project(project, "SDK lifetime");
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument document(scene);
        document.open_project(project);
        const auto cmake = path_utf8(self_executable());
        const auto work = project / ".forge/sdk-build";
        put(work / "build/legacy-marker.txt", "retain legacy cache");
        std::filesystem::path prior_cache, retained_stage;
        std::optional<std::string> saved_manifest;
        {
            SdkBuild build(project);
            build.create(document, sdk);
            saved_manifest = asset_storage::read(project / "forge.project.json");
            put(control / "mode.json", "\"error\"");
            build.build(document, sdk, cmake, "unused-ninja");
            finish(build, document);
            check(!build.error().empty(), "Failed compiler was accepted");
            if (!std::filesystem::exists(control / "observed.json"))
                throw std::runtime_error("Fixture compiler did not run: " + build.error() + "\n" +
                                         build.log());
            prior_cache = observed(control);
            check(read_json(work / "cache-ready.json").at("generation") ==
                      path_utf8(prior_cache.filename()),
                  "Retired compiler cache was not made reusable");
            put(control / "mode.json", "\"hold\"");
            std::filesystem::remove(control / "observed.json");
            build.build(document, sdk, cmake, "unused-ninja");
            check(!std::filesystem::exists(work / "cache-ready.json"),
                  "Active cache still had a reusable pointer");
            const auto deadline = SDL_GetTicks() + 5000;
            while (!std::filesystem::exists(control / "observed.json")) {
                build.pump(document);
                check(build.busy() && SDL_GetTicks() < deadline, "Compiler did not become ready");
                SDL_Delay(1);
            }
            check(observed(control) == prior_cache, "Normal retry discarded the incremental cache");
            build.cancel();
            check(build.busy(), "Cancel released SDK ownership before retirement");
            finish(build, document);
            check(std::filesystem::is_regular_file(work / "cache-ready.json"),
                  "Confirmed cancellation lost reusable cache");
#ifdef __linux__
            put(control / "mode.json", "\"crash\"");
            build.build(document, sdk, cmake, "unused-ninja");
            for (const auto& entry : std::filesystem::directory_iterator(work))
                if (entry.is_directory() && entry.path().filename() != "builds" &&
                    entry.path().filename() != "build")
                    retained_stage = entry.path();
            finish(build, document);
            check(build.error().find("retirement was not confirmed") != std::string::npos &&
                      !std::filesystem::exists(work / "cache-ready.json") &&
                      std::filesystem::is_regular_file(retained_stage / "cache-claim.json") &&
                      std::filesystem::is_regular_file(prior_cache / "compiler-output.txt"),
                  "Unconfirmed compiler work was removed or made reusable");
#endif
        }
#ifdef __linux__
        {
            SdkBuild reopened(project);
            put(control / "mode.json", "\"error\"");
            reopened.build(document, sdk, cmake, "unused-ninja");
            finish(reopened, document);
            check(observed(control) != prior_cache &&
                      std::filesystem::is_regular_file(prior_cache / "compiler-output.txt") &&
                      std::filesystem::is_regular_file(retained_stage / "cache-claim.json"),
                  "Reopened SDK owner reused or erased uncertain work");
        }
#endif
        {
            const auto ready = work / "cache-ready.json";
            const auto intact = asset_storage::read(ready);
            check(bool(intact), "Missing reusable cache pointer before recovery check");
            const auto malformed = *intact + std::string(1, '\0') + "hidden suffix";
            put(ready, malformed);
            std::filesystem::remove(control / "observed.json");
            SdkBuild recovery(project);
            bool rejected = false;
            try {
                recovery.build(document, sdk, cmake, "unused-ninja");
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && !recovery.busy() && asset_storage::read(ready) == malformed &&
                      !std::filesystem::exists(control / "observed.json"),
                  "Malformed cache pointer launched a compiler or was overwritten");
            put(ready, *intact);
        }
#ifndef _WIN32
        {
            const auto authored = project / "Assets/Retained";
            put(authored / "authored.txt", "saved authored bytes");
            std::filesystem::rename(work / "builds", work / "preserved-builds");
            std::filesystem::create_directory_symlink(authored, work / "builds");
            SdkBuild redirected(project);
            bool rejected = false;
            try {
                redirected.build(document, sdk, cmake, "unused-ninja");
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected &&
                      asset_storage::read(authored / "authored.txt") == "saved authored bytes" &&
                      std::distance(std::filesystem::directory_iterator(authored),
                                    std::filesystem::directory_iterator{}) == 1,
                  "Redirected private build storage changed authored data");
            std::filesystem::remove(work / "builds");
            std::filesystem::rename(work / "preserved-builds", work / "builds");
        }
#endif
        check(asset_storage::read(project / "forge.project.json") == saved_manifest,
              "Compiler failure/cancellation changed published project settings");
        check(asset_storage::read(work / "build/legacy-marker.txt") == "retain legacy cache",
              "Cache generation selection changed legacy build files");
        std::cout << "SDK owner cancellation, cache reuse and retained publication passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
