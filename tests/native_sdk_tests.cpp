#include <cmath>
#include <cstdlib>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
static void check(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> static void reject(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception&) {
        bad = true;
    }
    check(bad, "Expected native SDK rejection");
}
struct ProbeData {
    uint64_t ticks, presses;
    double dt;
};
static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    try {
        if (argc != 6)
            throw std::runtime_error("good bad failed trace arguments required");
        reject([&] {
            EngineContext absent(WorldRole::Runtime, false,
                                 {load_native_sdk(argv[5], "project.navigation_probe", "1")});
        });
        const auto trace = std::filesystem::absolute(argv[4]);
#ifdef _WIN32
        _putenv_s("FORGE_SDK_TRACE", trace.string().c_str());
#else
        setenv("FORGE_SDK_TRACE", trace.c_str(), 1);
#endif
        EngineServices services;
        reject([&] {
            load_native_sdk(std::filesystem::absolute(argv[2]), "project.sdk_probe", "1",
                            services.access());
        });
        check(services.access().diagnostics().back()["context"]["module"] == "project.sdk_probe",
              "Native module diagnostic absent");
        const auto root = std::filesystem::absolute(argv[1]).parent_path();
        Json declaration{{"id", "project.sdk_probe"},
                         {"sdk", "experimental-1"},
                         {"implementation", "1"},
                         {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                         {"library", std::filesystem::path(argv[1]).filename().string()},
                         {"dependencies", Json::array({"forge.input", "forge.transforms"})}};
        auto project = Json{{"modules", Json::array({declaration})}};
        check(project_native_modules(root, project).size() == 1,
              "Exact/local module declaration failed");
        auto invalid = project;
        invalid["modules"][0]["implementation"] = "2";
        reject([&] { project_native_modules(root, invalid); });
        invalid = project;
        invalid["modules"][0]["library"] = "missing-library";
        reject([&] { project_native_modules(root, invalid); });
        invalid = project;
        invalid["modules"][0]["fingerprint"] = std::string(64, '0');
        reject([&] { project_native_modules(root, invalid); });
        invalid = project;
        invalid["modules"][0]["dependencies"] = {"project.absent"};
        reject([&] { validate_project_modules(invalid); });
        invalid = project;
        invalid["modules"].push_back(declaration);
        reject([&] { validate_project_modules(invalid); });
        std::filesystem::remove(trace);
        auto sdk = load_native_sdk(std::filesystem::absolute(argv[1]), "project.sdk_probe", "1");
        std::weak_ptr<void> lease = sdk.code;
        Module legacy;
        {
            EngineContext engine(WorldRole::Runtime, false, {sdk});
            EngineContext second(WorldRole::Validation, false, {sdk});
            sdk = {};
            check(!lease.expired(), "Library not retained by world");
            auto& w = engine.world().world();
            auto entity = w.lookup("sdk.subject"), component = w.lookup("sdk.Probe");
            check(entity && component && second.world().world().lookup("sdk.Probe"),
                  "Cross DLL/world registration missing");
            check(!second.world().world().lookup("sdk.subject"),
                  "Runtime system activated in validation role");
            auto value = [&] {
                return *static_cast<const ProbeData*>(
                    ecs_get_id(w.c_ptr(), entity.id(), component.id()));
            };
            Scene scene(engine.world());
            RuntimeSimulation sim(engine.world(), scene, legacy);
            const auto id = ActionId::parse("12345678-1234-4234-8234-123456789abc");
            sim.input().configure(InputMap(
                {{"version", 1},
                 {"actions",
                  Json::array({{{"id", id},
                                {"name", "SDK Test"},
                                {"kind", "digital"},
                                {"bindings", Json::array({{{"control", "key.space"}}})}}})}}));
            sim.input().submit({{"key.space", 1}});
            RuntimeClock clock;
            auto tick = [&](float dt) { sim.tick(dt); };
            check(value().ticks == 0, "Paused SDK advanced");
            clock.step(tick);
            clock.step(tick);
            check(value().ticks == 2 && value().presses == 1 &&
                      std::abs(value().dt - 1.0 / 60) < 1e-6,
                  "SDK fixed dt/input snapshot failed");
            auto time = RuntimeClock::Time{};
            clock.resume(time);
            clock.advance(time + std::chrono::milliseconds(50), tick);
            clock.pause(time + std::chrono::milliseconds(50));
            check(value().ticks == 5 && value().presses == 1, "Resume/catch-up repeated edge");
            check(engine.services().diagnostics().back()["context"]["tick"] == 5,
                  "SDK diagnostic tick missing");
            check(read(trace).find("unload") == std::string::npos, "Library unloaded before world");
        }
        check(lease.expired(), "Code lease leaked");
        const auto log = read(trace);
        for (auto token : {"stop", "component_destructor", "system_context", "observer_context",
                           "module_context"})
            check(log.find(token) != std::string::npos && log.rfind(token) < log.find("unload"),
                  "Teardown callback after code unload");
        check(log.ends_with("unload\n"), "Code did not unload last");
        std::filesystem::remove(trace);
        reject([&] {
            EngineContext bad(
                WorldRole::Runtime, false,
                {load_native_sdk(std::filesystem::absolute(argv[3]), "project.sdk_probe", "1")});
        });
        auto failed = read(trace);
        check(failed.find("observer_context") < failed.find("unload") &&
                  failed.find("module_context") < failed.find("unload"),
              "Failed bootstrap unloaded callbacks early");
        std::cout << "Shared Flecs identity, direct registration, dt/input, roles, leases and "
                     "failed teardown passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
