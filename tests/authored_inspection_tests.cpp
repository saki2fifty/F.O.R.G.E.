#include "../src/asset_worker.hpp"
#include "../src/authored_component.hpp"
#include "../src/authored_inspection.hpp"
#include "../src/authored_schema.hpp"
#include "../src/worker_staging.hpp"
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <future>
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
static void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
#ifdef __linux__
// Only this isolated test controller adopts the fixture's escaped descendants.
struct Descendant {
    std::filesystem::path root;
    int previous = 0;
    explicit Descendant(std::filesystem::path path) : root(std::move(path)) {
        check(prctl(PR_GET_CHILD_SUBREAPER, &previous, 0L, 0L, 0L) == 0 &&
                  prctl(PR_SET_CHILD_SUBREAPER, 1L, 0L, 0L, 0L) == 0,
              "Cannot isolate schema fixture descendant");
    }
    pid_t id() const {
        pid_t value = -1;
        std::ifstream(root / "fixture.pid") >> value;
        return value;
    }
    void finish() {
        const auto child = id();
        check(child > 0, "Missing schema descendant identity");
        std::ofstream(root / "fixture.release") << "release";
        int status = 0;
        pid_t joined;
        do {
            joined = waitpid(child, &status, 0);
        } while (joined < 0 && errno == EINTR);
        std::filesystem::remove(root / "fixture.pid");
        check(joined == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "Schema descendant did not retire cleanly");
    }
    ~Descendant() {
        if (const auto child = id(); child > 0) {
            kill(child, SIGKILL);
            while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
            }
        }
        prctl(PR_SET_CHILD_SUBREAPER, long(previous), 0L, 0L, 0L);
    }
};
#endif
int main(int argc, char** argv) {
    try {
        if (argc != 8)
            throw std::runtime_error("Need runtime, good, failed, schema-crash, physics-required "
                                     "modules, worker fixture and migration module");
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
            started = std::filesystem::is_regular_file(root / "fixture.started");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        cancel.request_stop();
        rejects([&] { (void)job.get(); });
        check(started, "Cancellation fixture did not start its child process");
        std::filesystem::remove(root / "fixture.started");
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
        check(std::filesystem::is_regular_file(root / "fixture.started") &&
                  std::chrono::steady_clock::now() - before_timeout < std::chrono::seconds(10),
              "Schema worker deadline did not bound a running child");
        check(detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT) ==
                  result,
              "Clean retry did not preserve deterministic copied metadata");
        install(argv[7]);
        const auto current =
            detail::inspect_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT);
        const auto target = current.at("components")[0];
        auto source = result.at("components")[0];
        for (auto& field : source["structure"]["fields"])
            if (field.at("id") == "health")
                field["id"] = "hitpoints";
        source["defaults"]["hitpoints"] = source["defaults"].at("health");
        source["defaults"].erase("health");
        source["digest"] = detail::authored_structure_digest(source.at("structure"));
        auto value = detail::AuthoredCodec{0, source, {}}.defaults();
        value["hitpoints"] = 73.0;
        value["lives"] = UINT64_MAX;
        value["opaque"] = {{"hitpoints", "keep"}};
        Json values =
            Json::array({{{"value", value}, {"property_intent", false}},
                         {{"value", {{"$forge", value.at("$forge")}, {"hitpoints", 73.0}}},
                          {"property_intent", true}}});
        Json rules{{"aliases", Json::array({{{"path", {"hitpoints"}}, {"name", "health"}}})}};
        const auto migrated = detail::migrate_project_authoring(
            runtime, root, FORGE_NATIVE_SDK_FINGERPRINT, source, target, rules, values);
        check(migrated[0]["value"]["health"] == 73.0 &&
                  migrated[0]["value"]["lives"].get<std::uint64_t>() == UINT64_MAX &&
                  migrated[0]["value"]["opaque"] == value.at("opaque") &&
                  !migrated[1]["value"].contains("lives") &&
                  !migrated[0]["value"].contains("hitpoints"),
              "Isolated migration lost exact values, extensions or partial override intent");
        auto stale_target = target;
        stale_target["defaults"]["lives"] = 4u;
        rejects([&] {
            detail::migrate_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT, source,
                                              stale_target, rules, values);
        });
        auto collision = values;
        collision[0]["value"]["health"] = "opaque collision";
        rejects([&] {
            detail::migrate_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT, source,
                                              target, rules, collision);
        });
        rejects([&] {
            detail::migrate_project_authoring(runtime, root, FORGE_NATIVE_SDK_FINGERPRINT, source,
                                              target, rules, values, stopped.get_token());
        });
#ifdef __linux__
        install(argv[2]);
        atomic_write(root / "fixture-manifest.json", result.dump());
        for (const auto mode : {"escaped", "escaped-wait"}) {
            for (const auto name :
                 {"fixture.child-ready", "fixture.ready", "fixture.release", "fixture.stage"})
                std::filesystem::remove(root / name);
            Descendant descendant(root);
            atomic_write(root / "fixture.mode", mode);
            std::stop_source stop;
            auto pending = std::async(std::launch::async, [&] {
                return detail::inspect_project_authoring(
                    fixture, root, FORGE_NATIVE_SDK_FINGERPRINT, stop.get_token());
            });
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!std::filesystem::exists(root / "fixture.ready") &&
                   std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const bool cancel_run = std::string_view(mode) == "escaped-wait";
            if (cancel_run || !std::filesystem::exists(root / "fixture.ready"))
                stop.request_stop();
            bool rejected = false;
            try {
                check(pending.get() == result, "Schema retry changed copied metadata");
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected == cancel_run, "Schema completion/cancellation result changed");
            std::string stage;
            std::getline(std::ifstream(root / "fixture.stage"), stage);
            check(!stage.empty() && std::filesystem::is_regular_file(std::filesystem::path(stage) /
                                                                     "request.json"),
                  "Schema producer erased staging while an escaped descendant was still alive");
            ProjectLease lease(root);
            auto retained = asset_detail::cleanup_worker_stages(lease);
            check(retained.size() == 1 && retained[0].at("removed") == false,
                  "Maintenance removed a stage still owned by a schema descendant");
            descendant.finish();
            auto removed = asset_detail::cleanup_worker_stages(lease);
            check(removed.size() == 1 && removed[0].at("removed") == true &&
                      !std::filesystem::exists(stage),
                  "Retired schema stage could not be reclaimed safely");
        }
#endif
        std::cout
            << "Bounded isolated SDK schema extraction, crash/failure/cancel and retry passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
