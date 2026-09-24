// INTERNAL SDK EXAMPLE / TEST PROBE. No production gameplay feature or persisted component.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <flecs.h>
#include <forge/engine_assets.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
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
#ifdef FORGE_SDK_SCHEMA_MIGRATE
        constexpr uint32_t authored_version = 2;
#else
        constexpr uint32_t authored_version = 1;
#endif
        if (!host->authoring_type ||
            !host->authoring_type(host->context, health, "project.health", authored_version,
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
        if (forge::sdk::Client(host).available(forge::sdk::Capability::Game)) {
            ForgeSdkSaveSchemaV1 schema{};
            schema.size = sizeof(schema);
            schema.version = 2;
            schema.validate = [](void*, const char* text, char*, uint32_t) -> int32_t {
                try {
                    const auto save = nlohmann::json::parse(text);
                    (void)save.at("scene").get<forge::AssetId>();
                    return save.at("data").at("counter").is_number_integer() &&
                           save.at("data").at("counter") >= 0;
                } catch (...) {
                    return 0;
                }
            };
            schema.migrate = [](void*, uint32_t from, const char* text, char* output,
                                uint32_t capacity) -> int32_t {
                try {
                    if (from != 1)
                        return 0;
                    auto save = nlohmann::json::parse(text);
                    save["data"]["counter"] = save.at("data").at("score");
                    save["data"].erase("score");
                    const auto copy = save.dump();
                    if (copy.size() >= capacity)
                        return 0;
                    std::memcpy(output, copy.c_str(), copy.size() + 1);
                    return 1;
                } catch (...) {
                    return 0;
                }
            };
            const uint32_t migration = 1;
            schema.migration_versions = &migration;
            schema.migration_count = 1;
            if (!host->game_save_schema(host->context, &schema))
                throw std::runtime_error("SDK save schema registration failed");
        }
        if (const auto* asset = std::getenv("FORGE_SDK_PACKAGE_RESOURCE")) {
            forge::sdk::Client client(host);
            const auto token = client.request_resource(FORGE_SDK_RESOURCE_TEXTURE, asset);
            if (!token)
                throw std::runtime_error("Declared dynamic resource was rejected");
            const auto* undeclared = std::getenv("FORGE_SDK_PACKAGE_UNDECLARED");
            if (!undeclared || client.request_resource(FORGE_SDK_RESOURCE_TEXTURE, undeclared))
                throw std::runtime_error("Undeclared dynamic resource was accepted");
            w.entity("sdk.dynamic_resource").set<uint64_t>(token);
        }
        // Opt-in executable fixture, also built solely from the relocated SDK.
        // Startup queues membership; the fixed system uses ordinary deferred
        // Flecs writes after the engine reports the registered entity Ready.
        if (const auto* enabled = std::getenv("FORGE_SDK_SPAWN_TEST");
            enabled && std::strcmp(enabled, "1") == 0) {
            const auto token = forge::sdk::Client(host).request_entity("SDK runtime mesh");
            if (!token)
                throw std::runtime_error("Startup runtime entity request rejected");
            auto done = std::make_shared<bool>(false);
            w.system()
                .kind(host->fixed_phase)
                .write<forge::MeshRenderer>()
                .write<forge::LocalTranslation>()
                .run([host, token, done](flecs::iter& it) {
                    if (*done)
                        return;
                    forge::sdk::Client client(host);
                    ForgeSdkEntityV1 result{};
                    if (!client.inspect_entity(token, result))
                        throw std::runtime_error("Lost startup entity request");
                    if (result.state == FORGE_SDK_ENTITY_PENDING)
                        return;
                    if (result.state != FORGE_SDK_ENTITY_READY)
                        throw std::runtime_error(result.diagnostic);
                    forge::MeshRenderer renderer;
                    renderer.mesh = forge::engine_primitive(0);
                    it.world()
                        .entity(result.native_entity)
                        .set<forge::MeshRenderer>(renderer)
                        .set<forge::LocalTranslation>({4, 5, 6});
                    if (!client.release_entity(token))
                        throw std::runtime_error("Cannot release entity observation");
                    *done = true;
                })
                .add(host->fixed_tag);
        }
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
        w.system<const AuthoredHealth>()
            .kind(host->fixed_phase)
            .each([host](const AuthoredHealth& value) {
                char message[160]{};
                std::snprintf(message, sizeof(message), "SDK authored health %.17g lives %llu",
                              value.health, static_cast<unsigned long long>(value.lives));
                host->diagnostic(host->context, 1, message);
            })
            .add(host->fixed_tag);
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
int32_t FORGE_SDK_CALL controls(const ForgeSdkWorldV1* host, char* error, uint32_t size) {
    try {
        forge::sdk::Client client(host);
        if (!client.available(forge::sdk::Capability::Game))
            return 1;
        if (!client.callable(forge::sdk::Capability::ControlInput) ||
            client.callable(forge::sdk::Capability::Input))
            throw std::runtime_error("SDK control callback has wrong input domain");
        const auto token = host->game_request(host->context, R"({"operation":"pause"})");
        if (!token)
            throw std::runtime_error("SDK control request rejected");
        flecs::world(host->world).entity("sdk.game_token").set<uint64_t>(token);
        return 1;
    } catch (const std::exception& e) {
        std::snprintf(error, size, "%s", e.what());
        return 0;
    }
}
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
                                  FORGE_SDK_RESOURCES | FORGE_SDK_PHYSICS | FORGE_SDK_GAME,
#else
                              FORGE_SDK_DIAGNOSTICS,
                              FORGE_SDK_DIAGNOSTICS | FORGE_SDK_PROFILING | FORGE_SDK_UI |
                                  FORGE_SDK_RESOURCES | FORGE_SDK_GAME,
#endif
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              stop,
                              controls};
} // namespace forge_sdk_example
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &forge_sdk_example::api;
}
