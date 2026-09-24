#include "sdk_entity_tests.hpp"
#include "sdk_game_tests.hpp"
#include "sdk_package_resource_tests.hpp"
#include "sdk_resource_tests.hpp"
#include <cmath>
#include <cstdlib>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/sdk_client.hpp>
#include <fstream>
#include <iostream>
#include <thread>
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
struct StubUi : UiService {
    void publish(EntityId, const std::string&, const Json&) override {}
    void allow_action(const std::string&) override {}
    std::optional<UiAction> poll_action(const std::string&) override { return {}; }
};
static EngineModule late_ui() {
    EngineModule m;
    m.id = "zz.late_ui";
    m.runtime_roles = role_mask(WorldRole::Runtime);
    m.allowed_services = m.provided_services = capability(Capability::Ui);
    m.start = [](ModuleContext& c) {
        auto service = std::make_shared<StubUi>();
        c.state = service;
        c.services.publish_ui(service);
    };
    m.stop = [](ModuleContext& c) { c.services.publish_ui({}); };
    return m;
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
        if (argc != 7)
            throw std::runtime_error("good bad failed trace arguments required");
        reject([&] {
            EngineContext absent(WorldRole::Runtime, false,
                                 {load_native_sdk(argv[5], "project.navigation_probe", "1")});
        });
        {
            EngineContext optional(WorldRole::Runtime, true,
                                   {load_native_sdk(argv[6], "project.example", "1")});
            Scene scene(optional.world());
            Module legacy;
            RuntimeSimulation sim(optional.world(), scene, legacy);
            sim.tick(1.f / 60);
            bool observed = false;
            for (const auto& d : optional.services().diagnostics())
                observed |= d.at("text").get<std::string>().find("physics=0 navigation=0 ui=0") !=
                            std::string::npos;
            check(observed, "Combined sample failed with optional providers omitted");
        }
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
#ifdef _WIN32
            _putenv_s("FORGE_SDK_SPAWN_TEST", "1");
#else
            setenv("FORGE_SDK_SPAWN_TEST", "1", 1);
#endif
            EngineContext engine(WorldRole::Runtime, true,
                                 {sdk, late_ui(), runtime_resources_module(root)});
#ifdef _WIN32
            _putenv_s("FORGE_SDK_SPAWN_TEST", "");
#else
            unsetenv("FORGE_SDK_SPAWN_TEST");
#endif
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
            const auto* host = *static_cast<const ForgeSdkWorldV1* const*>(
                ecs_get_id(w.c_ptr(), w.lookup("sdk.host").id(), w.lookup("sdk.HostProbe").id()));
            test_sdk_resources(host, engine.services());
            test_sdk_entities(host, engine.world());
            sdk::Client client(host);
            check(client.valid() && client.available(sdk::Capability::Ui) &&
                      !(host->capabilities & FORGE_SDK_UI),
                  "Live optional provider discovery is stale");
            check(client.available(sdk::Capability::Input) &&
                      !client.callable(sdk::Capability::Input),
                  "Input availability must differ from current fixed-tick callability");
            check(!client.available(sdk::Capability::Physics), "Missing/restricted physics leaked");
            auto version = client.query(sdk::Capability::Ui, 99);
            check(version.version == 1 && !version.available && !version.callable,
                  "Wrong capability version accepted");
            ForgeSdkCapabilityV1 unknown{sizeof(unknown), 9, 9, 9, 9};
            check(!host->query_capability(host->context, 3, 1, &unknown) && !unknown.available,
                  "Composite/unknown capability accepted");
            check(!host->query_capability(nullptr, FORGE_SDK_UI, 1, &unknown),
                  "Null query context accepted");
            check(!host->audio_source(nullptr, nullptr, nullptr, 1) &&
                      !host->read_action(nullptr, nullptr, nullptr) &&
                      host->raycast(nullptr, nullptr, nullptr, nullptr) == -1 &&
                      !host->physics_move(nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0) &&
                      !host->diagnostic(nullptr, 1, "invalid") &&
                      !host->profile_sample(nullptr, "invalid", 0),
                  "Null callback context was not rejected");
            bool foreign = false;
            std::thread worker([&] {
                foreign = !client.available(sdk::Capability::Ui) &&
                          !client.diagnostic(1, "wrong thread") &&
                          !client.profile("wrong thread", 0);
            });
            worker.join();
            check(foreign, "SDK allowed foreign-thread capability access");
            check(!client.allow_action("TooLate"),
                  "UI action registration accepted outside startup");
            check(client.profile("SDK sample", .001), "SDK profiling sample rejected");
#ifndef FORGE_DISABLE_PROFILING
            check(engine.services().profiles().back()["category"] == "project.sdk_probe",
                  "SDK profiling sample missing module context");
#endif
            check(!client.profile("invalid", -1) && !client.profile("invalid", NAN),
                  "Invalid profile sample accepted");
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
            const auto rendered = extract_render_scene(sim.presentation(1));
            check(rendered.meshes.size() == 1 &&
                      rendered.meshes.front().renderer.mesh == engine_primitive(0) &&
                      rendered.meshes.front().world.m[3] == 4 &&
                      rendered.meshes.front().world.m[7] == 5 &&
                      rendered.meshes.front().world.m[11] == 6 && !scene.can_undo(),
                  "SDK deferred native render writes did not reach presentation");
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
            const auto cancelled_on_stop = client.request_entity("Never created after stop");
            check(cancelled_on_stop != 0, "Could not queue shutdown cancellation fixture");
            engine.world().modules().stop();
            detail::publish_runtime_entities(engine.world());
            check(scene.entity_count() == 1 && !client.request_entity("Stopped") &&
                      !client.available(sdk::Capability::RuntimeEntities),
                  "Stopped SDK retained/published entity creation requests");
            check(!client.available(sdk::Capability::Ui) &&
                      !client.available(sdk::Capability::Input),
                  "Stopped SDK retains gameplay capability");
            check(client.diagnostic(1, "safe shutdown diagnostic"),
                  "Shutdown diagnostic bridge retired too soon");
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
        test_sdk_package_resources(std::filesystem::absolute(argv[1]), root);
        test_sdk_game(std::filesystem::absolute(argv[1]));
        std::cout << "Shared Flecs identity, direct registration, dt/input, roles, leases and "
                     "failed teardown passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
