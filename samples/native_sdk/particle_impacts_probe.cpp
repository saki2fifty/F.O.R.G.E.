// Original exact-SDK impact observation and permission acceptance fixture.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

namespace {
using Json = nlohmann::json;
#ifdef FORGE_TEST_PARTICLE_IMPACTS_DENIED
constexpr bool denied = true;
constexpr const char* module_id = "project.particle_impacts_denied";
#else
constexpr bool denied = false;
constexpr const char* module_id = "project.particle_impacts_probe";
#endif
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
ForgeSdkParticleImpactV1 dirty() {
    ForgeSdkParticleImpactV1 value{};
    value.size = sizeof(value);
    value.tick = value.birth = value.source_generation = 42;
    value.kind = value.seed = value.query_space = 42;
    value.age_seconds = value.step_time_seconds = 42;
    std::strcpy(value.effect, "stale");
    std::strcpy(value.emitter_entity, "stale");
    value.position[1] = value.normal[1] = value.incoming_velocity[1] = value.outgoing_velocity[1] =
        42;
    return value;
}
Json copied(const ForgeSdkParticleImpactV1& value) {
    auto vector = [](const double (&data)[3]) { return Json::array({data[0], data[1], data[2]}); };
    return {{"size", value.size},
            {"kind", value.kind},
            {"seed", value.seed},
            {"space", value.query_space},
            {"tick", value.tick},
            {"birth", value.birth},
            {"generation", value.source_generation},
            {"effect", value.effect},
            {"emitter_scene", value.emitter_scene},
            {"emitter", value.emitter_entity},
            {"collider_scene", value.collider_scene},
            {"collider", value.collider_entity},
            {"age", value.age_seconds},
            {"step_time", value.step_time_seconds},
            {"position", vector(value.position)},
            {"normal", vector(value.normal)},
            {"incoming", vector(value.incoming_velocity)},
            {"outgoing", vector(value.outgoing_velocity)}};
}
bool cleared(const ForgeSdkParticleImpactV1& value) {
    ForgeSdkParticleImpactV1 empty{};
    empty.size = sizeof(empty);
    return copied(value) == copied(empty);
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1*, char*, uint32_t) { return 1; }
int32_t FORGE_SDK_CALL outside(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    auto value = dirty();
    if (host->particle_impact_count(host->context) != -1 ||
        host->particle_impact_event(host->context, 0, &value) != -1 || !cleared(value)) {
        if (capacity)
            std::snprintf(error, capacity, "Impact read accepted outside a fixed tick");
        return 0;
    }
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    if (!outside(host, error, capacity))
        return 0;
    flecs::world(host->world)
        .system<>()
        .kind(host->post_physics_phase)
        .run([host](flecs::iter&) {
            const forge::sdk::Client client(host);
            const auto count = host->particle_impact_count(host->context);
            require(count == client.particle_impact_count() &&
                        (denied ? count == -1
                                : count >= 0 && uint32_t(count) <= FORGE_SDK_PARTICLE_IMPACT_LIMIT),
                    "Impact count or Physics permission mismatch");
            auto invalid = dirty();
            require(host->particle_impact_event(host->context, UINT32_MAX, &invalid) ==
                            (denied ? -1 : 0) &&
                        cleared(invalid),
                    "Impact index or denied output guard failed");
            invalid = dirty();
            require(client.particle_impact_event(UINT32_MAX, invalid) == (denied ? -1 : 0) &&
                        cleared(invalid),
                    "Impact client left stale output");
            invalid = dirty();
            invalid.size -= 1;
            const auto before = copied(invalid);
            require(host->particle_impact_event(host->context, 0, nullptr) == -1 &&
                        host->particle_impact_event(host->context, 0, &invalid) == -1 &&
                        copied(invalid) == before,
                    "Impact callback wrote an invalid-sized output");
            bool foreign_rejected = false;
            std::thread foreign([&] {
                auto value = dirty();
                foreign_rejected = host->particle_impact_count(host->context) == -1 &&
                                   host->particle_impact_event(host->context, 0, &value) == -1 &&
                                   cleared(value);
            });
            foreign.join();
            require(foreign_rejected, "Impact read accepted a foreign thread");
            Json events = Json::array();
            for (int32_t index = 0; index < count; ++index) {
                auto value = dirty(), repeated = dirty();
                const auto result = host->particle_impact_event(host->context, index, &value);
                require(result >= 0 && client.particle_impact_event(index, repeated) == result &&
                            copied(value) == copied(repeated),
                        "Impact slot was consumed or changed between reads");
                if (!result) {
                    require(cleared(value), "Retired impact slot retained stale output");
                    continue;
                }
                require(value.tick && value.source_generation && value.kind <= 2 &&
                            value.query_space <= 1 && std::isfinite(value.age_seconds) &&
                            value.age_seconds > 0 && std::isfinite(value.step_time_seconds) &&
                            value.step_time_seconds >= 0,
                        "Impact observation lost valid provenance");
                events.push_back(copied(value));
            }
            require(host->particle_impact_count(host->context) == count,
                    "Impact read changed the stable slot count");
            const auto message =
                "Particle impacts: " + Json{{"count", count}, {"events", events}}.dump();
            require(host->diagnostic(host->context, 1, message.c_str()) == 1,
                    "Impact acceptance diagnostic rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              module_id,
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              denied ? 0u : FORGE_SDK_PHYSICS,
                              FORGE_SDK_DIAGNOSTICS | (denied ? 0u : FORGE_SDK_PHYSICS),
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              outside};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
