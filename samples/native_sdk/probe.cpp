// INTERNAL SDK EXAMPLE / TEST PROBE. No production gameplay feature or persisted component.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <flecs.h>
#include <forge/engine_assets.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <memory>
#include <stdexcept>
namespace forge_sdk_example {
void trace(const char* text) {
    const char* path = std::getenv("FORGE_SDK_TRACE");
    if (!path)
        return;
    if (auto* f = std::fopen(path, "a")) {
        std::fprintf(f, "%s\n", text);
        std::fclose(f);
    }
}
struct LibrarySentinel {
    ~LibrarySentinel() { trace("unload"); }
} library;
struct HostProbe {
    const ForgeSdkWorldV1* host;
};
struct AuthoredHealth {
    double health = 100;
    uint64_t lives = 3;
};
struct Probe {
    uint64_t ticks = 0, presses = 0;
    double dt = 0;
    ~Probe() { trace("component_destructor"); }
};
struct Marker {
    const char* name;
    const ForgeSdkWorldV1* host;
    Marker(const char* n, const ForgeSdkWorldV1* h) : name(n), host(h) {}
    ~Marker() {
        trace(name);
        host->diagnostic(host->context, 1, name);
    }
};
struct ProbeModule {
    explicit ProbeModule(flecs::world& w) {
        w.module<ProbeModule>();
        auto previous = ecs_set_scope(w.c_ptr(), 0);
        w.component<Probe>("sdk.Probe");
        ecs_set_scope(w.c_ptr(), previous);
    }
    ~ProbeModule() { trace("module_context"); }
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t n) {
    try {
        flecs::world w(host->world);
#ifdef FORGE_SDK_SCHEMA_CRASH
        std::abort(); // Deliberate worker-isolation regression, never a shipped module.
#endif
        w.import<ProbeModule>();
        auto marker = std::make_shared<Marker>("observer_context", host);
        w.observer<Probe>().event(flecs::OnAdd).each([marker](Probe&) {
            trace("observer_called");
        });
#ifdef FORGE_SDK_FAIL
        w.entity("sdk.failed").set<Probe>({});
        throw std::runtime_error("intentional unpublished bootstrap failure");
#endif
        const auto health = w.component<AuthoredHealth>("sdk.AuthoredHealth")
                                .member<double>("health")
                                .member<uint64_t>("lives");
        ecs_doc_set_name(w, health, "SDK Health");
        ecs_doc_set_brief(w, health, "Reflected project health used by the SDK authoring proof.");
        if (!host->authoring_type ||
            !host->authoring_type(host->context, health, "project.health", 1,
                                  R"({"health":100,"lives":3})", "Gameplay", error, n))
            return 0;
        trace("schema");
        return 1;
    } catch (const std::exception& e) {
        if (n)
            std::snprintf(error, n, "%s", e.what());
        return 0;
    } catch (...) {
        if (n)
            std::snprintf(error, n, "Unexpected native registration failure");
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t n) {
    try {
        flecs::world w(host->world);
        char late[256]{};
        if (host->authoring_type(host->context, w.id<AuthoredHealth>(), "project.late", 1,
                                 R"({"health":100,"lives":3})", "Gameplay", late, sizeof(late)) ||
            !std::strstr(late, "schema-registration-only"))
            throw std::runtime_error("Late authoring opt-in escaped schema-stage guard");
        w.component<HostProbe>("sdk.HostProbe");
        w.entity("sdk.host").set<HostProbe>({host});
        auto marker = std::make_shared<Marker>("system_context", host);
        w.system<Probe>()
            .kind(host->fixed_phase)
            .each([host, marker](flecs::iter& it, size_t, Probe& p) {
#ifdef FORGE_SDK_CRASH
                std::abort();
#endif
                ++p.ticks;
                p.dt = it.delta_time();
                trace("tick");
                ForgeSdkActionV1 action{};
                action.size = sizeof(action);
                if (host->read_action(host->context, "12345678-1234-4234-8234-123456789abc",
                                      &action))
                    p.presses += action.pressed;
                host->diagnostic(host->context, 1, "SDK fixed tick");
            })
            .add(host->fixed_tag);
        w.entity("sdk.subject").set<Probe>({});
        host->diagnostic(host->context, 1, "SDK started");
        trace("start");
        return 1;
    } catch (const std::exception& e) {
        if (n)
            std::snprintf(error, n, "%s", e.what());
        return 0;
    } catch (...) {
        if (n)
            std::snprintf(error, n, "Unexpected native registration failure");
        return 0;
    }
}
void FORGE_SDK_CALL stop(const ForgeSdkWorldV1*) { trace("stop"); }
#ifdef FORGE_SDK_SCHEMA_PHYSICS
const char* deps[] = {"forge.input", "forge.transforms", "forge.physics"};
#else
const char* deps[] = {"forge.input", "forge.transforms"};
#endif
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
#ifdef FORGE_SDK_BAD
                              "incompatible",
#else
                              FORGE_NATIVE_SDK_FINGERPRINT,
#endif
                              "project.sdk_probe",
                              "1",
                              deps,
                              uint32_t(sizeof(deps) / sizeof(deps[0])),
                              FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                              FORGE_SDK_RUNTIME,
#ifdef FORGE_SDK_SCHEMA_PHYSICS
                              FORGE_SDK_DIAGNOSTICS | FORGE_SDK_PHYSICS,
                              FORGE_SDK_DIAGNOSTICS | FORGE_SDK_PROFILING | FORGE_SDK_UI |
                                  FORGE_SDK_PHYSICS,
#else
                              FORGE_SDK_DIAGNOSTICS,
                              FORGE_SDK_DIAGNOSTICS | FORGE_SDK_PROFILING | FORGE_SDK_UI,
#endif
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              stop};
} // namespace forge_sdk_example
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &forge_sdk_example::api;
}
