#include "../src/asset_worker.hpp"
#include "../src/authored_inspection.hpp"
#include "../src/authored_schema.hpp"
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
using namespace forge;
static void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char** argv) {
    try {
        if (argc != 7)
            throw std::runtime_error("Need runtime, good, failed, schema-crash, physics-required "
                                     "modules and worker fixture");
        const auto runtime = std::filesystem::absolute(argv[1]);
        const auto root =
            runtime.parent_path() / ("schema-inspection-" + AssetId::generate().str());
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(path, e);
            }
        } cleanup{root};
        auto install = [&](const char* input) {
            const auto library = std::filesystem::absolute(input);
            std::filesystem::copy_file(library, root / library.filename(),
                                       std::filesystem::copy_options::overwrite_existing);
            Json project{{"version", 2},
                         {"name", "SDK schema proof"},
                         {"startup_scene", nullptr},
                         {"input", {{"version", 1}, {"actions", Json::array()}}},
                         {"modules",
                          Json::array({{{"id", "project.sdk_probe"},
                                        {"implementation", "1"},
                                        {"sdk", "experimental-1"},
                                        {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                                        {"library", library.filename().string()},
                                        {"dependencies", {"forge.input", "forge.transforms"}}}})}};
            if (std::string(input) == argv[5])
                project["modules"][0]["dependencies"].push_back("forge.physics");
            atomic_write(root / "forge.project.json", project.dump());
        };
        install(argv[2]);
        const auto result =
            detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT);
        EngineContext owner(WorldRole::Authoring);
        detail::validate_authored_types(owner.world().world(), result.at("components"));
        check(result.at("components").size() == 1 &&
                  result.at("components")[0].at("id") == "project.health",
              "Bounded SDK worker did not return the opted-in type");
        auto rejects = [&](auto work) {
            bool failed = false;
            try {
                work();
            } catch (const std::exception&) {
                failed = true;
            }
            check(failed, "Expected schema worker rejection");
            check(std::filesystem::is_empty(root / ".forge/schema-jobs"),
                  "Inspection staging survived completion/failure");
            check(result.at("components")[0].at("defaults").at("health") == 100,
                  "Prior copied schema was changed on failure");
        };
        rejects([&] { detail::inspect_project_authoring(runtime, root, std::string(64, '0')); });
        std::stop_source stopped;
        stopped.request_stop();
        rejects([&] {
            detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT,
                                              stopped.get_token());
        });
        install(argv[3]);
        rejects([&] {
            detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT);
        });
        install(argv[4]);
        rejects([&] {
            detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT);
        });
        install(argv[5]);
        check(detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT)
                      .at("components") == result.at("components"),
              "Metadata-only inspection required an active physics simulation provider");
        install(argv[2]);
        const auto fixture = std::filesystem::absolute(argv[6]);
        for (const auto mode : {"duplicate", "large", "profile"}) {
            atomic_write(root / "fixture.mode", mode);
            rejects([&] {
                detail::inspect_project_authoring(fixture, root, FORGE_NATIVE_SDK_FINGERPRINT);
            });
        }
        atomic_write(root / "fixture.mode", "wait");
        std::stop_source cancel;
        auto job = std::async(std::launch::async, [&] {
            return detail::inspect_project_authoring(fixture, root, FORGE_NATIVE_SDK_FINGERPRINT,
                                                     cancel.get_token());
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool started = false;
        while (!started && std::chrono::steady_clock::now() < deadline) {
            for (const auto& entry :
                 std::filesystem::directory_iterator(root / ".forge/schema-jobs"))
                started |= std::filesystem::is_regular_file(entry.path() / "started.txt");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        cancel.request_stop();
        rejects([&] { (void)job.get(); });
        check(started, "Cancellation fixture did not start its child process");
        const auto timeout_dir = root / "timeout-proof";
        std::filesystem::create_directory(timeout_dir);
        atomic_write(timeout_dir / "request.json", Json{{"project", root.string()}}.dump());
        asset_detail::WorkerLimits limits;
        limits.seconds = 1;
        const auto before_timeout = std::chrono::steady_clock::now();
        rejects([&] {
            asset_detail::run_worker(asset_detail::WorkerKind::Schema, fixture, timeout_dir, {},
                                     limits);
        });
        check(std::filesystem::is_regular_file(timeout_dir / "started.txt") &&
                  std::chrono::steady_clock::now() - before_timeout < std::chrono::seconds(10),
              "Schema worker deadline did not bound a running child");
        check(detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT) ==
                  result,
              "Clean retry did not preserve deterministic copied metadata");
        std::cout
            << "Bounded isolated SDK schema extraction, crash/failure/cancel and retry passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
