#include <forge/native_sdk.hpp>
#include <forge/world.hpp>
#include <iostream>
#include <stdexcept>
using namespace forge;
static void check(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> static void reject(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Expected failure");
}
struct SourceProbe {
    explicit SourceProbe(flecs::world& w) { w.module<SourceProbe>("test::SourceProbe"); }
};
int main() {
    try {
        std::vector<std::string> events;
        struct Lease {
            std::vector<std::string>& events;
            ~Lease() { events.push_back("code"); }
        };
        auto code = std::make_shared<Lease>(Lease{events});
        events.clear();
        std::weak_ptr<Lease> weak = code;
        EngineModule a;
        a.id = "test.provider";
        a.dependencies = {"forge.core"};
        a.code = code;
        a.allowed_services = capability(Capability::Diagnostics);
        a.required_services = a.allowed_services;
        a.runtime_roles = role_mask(WorldRole::Runtime);
        a.schemas = [&](ModuleContext& c) {
            check(!weak.expired(), "Code gone during schema");
            check(c.services.available(Capability::Diagnostics) &&
                      !c.services.available(Capability::Profiling),
                  "Capability isolation");
            const auto x = c.world.import<SourceProbe>();
            const auto y = c.world.import<SourceProbe>();
            check(x == y, "Import repeated");
            events.push_back("schema");
            ecs_atfini(
                c.world.c_ptr(),
                [](ecs_world_t*, void* p) {
                    static_cast<std::vector<std::string>*>(p)->push_back("world");
                },
                &events);
        };
        a.start = [&](ModuleContext&) { events.push_back("start_a"); };
        a.stop = [&](ModuleContext&) { events.push_back("stop_a"); };
        EngineModule b;
        b.id = "test.consumer";
        b.dependencies = {"test.provider"};
        b.code = code;
        b.runtime_roles = a.runtime_roles;
        b.start = [&](ModuleContext&) { events.push_back("start_b"); };
        b.stop = [&](ModuleContext&) { events.push_back("stop_b"); };
        {
            EngineContext first(WorldRole::Runtime, false, {b, a});
            EngineContext second(WorldRole::Validation, false, {a});
            check(std::count(events.begin(), events.end(), "schema") == 2, "Not once per world");
            check(std::count(events.begin(), events.end(), "start_a") == 1,
                  "World role filtering failed");
            reject([&] {
                first.world().modules().bootstrap(first.world().world(), WorldRole::Runtime,
                                                  first.services(), {});
            });
            a.code.reset();
            b.code.reset();
            code.reset();
            check(!weak.expired(), "World did not retain code");
        }
        check(weak.expired() && events.back() == "code", "Code not released after world");
        auto sb = std::find(events.begin(), events.end(), "stop_b"),
             sa = std::find(events.begin(), events.end(), "stop_a");
        check(sb < sa, "Shutdown dependency order");
        EngineServices diagnostics;
        auto bad = a;
        bad.id = "test.bad";
        bad.dependencies = {"test.missing"};
        reject([&] { WorldContext w(WorldRole::Runtime, diagnostics.access(), {bad}); });
        check(diagnostics.access().diagnostics().back()["context"]["module"] == "test.bad",
              "Module diagnostic context absent");
        bad.dependencies = {"test.bad"};
        reject([&] { EngineContext w(WorldRole::Runtime, false, {bad}); });
        reject([&] { EngineContext w(WorldRole::Runtime, false, {a, a}); });
        bad = a;
        bad.allowed_services = 3;
        bad.required_services = 3;
        reject(
            [&] { WorldContext w(WorldRole::Runtime, diagnostics.access().restricted(1), {bad}); });
        bad = a;
        bad.schema_roles = role_mask(WorldRole::Authoring);
        bad.runtime_roles = 0;
        reject([&] { EngineContext w(WorldRole::Runtime, false, {b, bad}); });
        events.clear();
        a.schemas = [&](ModuleContext& c) {
            ecs_atfini(
                c.world.c_ptr(),
                [](ecs_world_t*, void* p) {
                    static_cast<std::vector<std::string>*>(p)->push_back("failed_world");
                },
                &events);
        };
        b.start = [](ModuleContext&) { throw std::runtime_error("Test startup failure"); };
        reject([&] { EngineContext w(WorldRole::Runtime, false, {a, b}); });
        check(events == std::vector<std::string>{"start_a", "stop_b", "stop_a", "failed_world"},
              "Failed bootstrap shutdown/world order");
        validate_project_modules({{"modules", {"core", "forge.input"}}});
        reject([&] { validate_project_modules({{"modules", {"core", "forge.core"}}}); });
        reject([&] { validate_project_modules({{"modules", {"unknown.module"}}}); });
        std::cout
            << "Module bootstrap, roles, capabilities, teardown and project declarations passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
