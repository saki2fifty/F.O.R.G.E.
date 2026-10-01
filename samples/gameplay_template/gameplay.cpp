// Exact-version FORGE gameplay. Registrations live in isolated runtimes only.
#include "forge.registration.hpp"
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
namespace {
// Plain reflected authored data. Flecs reflection is admitted explicitly below.
struct GameplayCounter {
    double rate = 1;
    double value = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto type = world.component<GameplayCounter>("gameplay.Counter")
                              .member<double>("rate")
                              .member<double>("value");
        ecs_doc_set_name(world, type, "Gameplay Counter");
        ecs_doc_set_brief(world, type,
                          "Increases value by rate each simulation second during Play.");
        if (!host->authoring_type(host->context, type, "project.counter", 1,
                                  R"({"rate":1,"value":0})", "Gameplay", error, capacity))
            return 0;
        return forge_register_components(host, error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Gameplay schema registration failed");
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.system<GameplayCounter>("gameplay.Count")
            .kind(host->fixed_phase)
            .each([](flecs::iter& it, size_t, GameplayCounter& counter) {
                counter.value += counter.rate * it.delta_time();
            })
            .add(host->fixed_tag);
        forge_register_systems(host);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Gameplay startup failed");
        return 0;
    }
}
void FORGE_SDK_CALL stop(const ForgeSdkWorldV1*) {}
const ForgeNativeSdkV1 descriptor = {sizeof(ForgeNativeSdkV1),
                                     FORGE_NATIVE_SDK_ABI,
                                     FORGE_NATIVE_SDK_FINGERPRINT,
                                     "project.gameplay",
                                     "1",
                                     nullptr,
                                     0,
                                     FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                                     FORGE_SDK_RUNTIME,
                                     0,
                                     0,
                                     &ecs_init,
                                     &ecs_os_api,
                                     schema,
                                     start,
                                     stop,
                                     nullptr,
                                     nullptr,
                                     nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &descriptor;
}
