// Internal exact SDK fixture: gameplay-owned Health, copied UI values and semantic actions.
#include <algorithm>
#include <flecs.h>
#include <forge/identity.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/ui_components.hpp>
#include <map>
namespace {
struct Health {
    double value = 100;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.component<forge::UiDocument>("forge.ui_document");
    w.component<forge::StableId>("forge.stable_id");
    w.component<Health>("project.UiHealth");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char*, uint32_t) {
    if (!(h->capabilities & FORGE_SDK_UI) || !h->ui_allow_action(h->context, "DecreaseHealth"))
        return 0;
    flecs::world w(h->world);
    w.system()
        .kind(h->fixed_phase)
        .run([h](flecs::iter&) {
            flecs::world world(h->world);
            std::map<std::string, unsigned> changes;
            char entity[37]{};
            while (h->ui_poll_action(h->context, "DecreaseHealth", entity, sizeof(entity)) == 1)
                ++changes[entity];
            world.each([&](flecs::entity e, const forge::UiDocument&, const forge::StableId& id) {
                auto health = e.has<Health>() ? e.get<Health>() : Health{};
                health.value = std::max(0., health.value - 10 * changes[id.value]);
                e.set(health);
                if (!h->ui_publish_number(h->context, id.value.c_str(), "health", health.value))
                    h->diagnostic(h->context, 3, "UI SDK publication failed");
            });
        })
        .add(h->fixed_tag);
    return 1;
}
const char* deps[] = {"forge.ui", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(api),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.ui_probe",
                              "1",
                              deps,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_UI,
                              FORGE_SDK_UI | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
