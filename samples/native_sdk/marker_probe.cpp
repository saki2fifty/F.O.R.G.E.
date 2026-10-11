// A real matching-SDK consumer. Observations are copied and read after animation.
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/identity.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <memory>
#include <optional>
#include <stdexcept>
void forge_register_system_MarkerPulse(const ForgeSdkWorldV1* host);
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void checked(const ForgeSdkWorldV1* host, F action) noexcept {
    try {
        action();
    } catch (const std::exception& e) {
        host->diagnostic(host->context, 3, e.what());
    } catch (...) {
        host->diagnostic(host->context, 3, "Marker probe callback failed");
    }
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world world(h->world);
    world.component<forge::Animator>("forge.animator");
    world.component<forge::StableId>("forge.stable_id");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        ForgeSdkAnimationEventV1 outside{};
        outside.size = sizeof(outside);
        check(h->animation_read_event(h->context, 0, &outside) == 0,
              "Markers readable outside fixed tick");
        outside.size = 0;
        check(h->animation_read_event(h->context, 0, &outside) == -1,
              "Invalid event size accepted");
        flecs::world w(h->world);
        w.system()
            .kind(h->fixed_phase)
            .run([h](flecs::iter&) {
                checked(h, [&] {
                    ForgeSdkAnimationEventV1 event{};
                    event.size = sizeof(event);
                    check(h->animation_read_event(h->context, 0, &event) == 0,
                          "Previous tick markers replayed before animation");
                });
            })
            .add(h->fixed_tag);
        auto removed = std::make_shared<std::optional<std::uint32_t>>();
        w.system()
            .kind(h->post_physics_phase)
            .write<forge::Animator>()
            .run([h, removed](flecs::iter& it) {
                checked(h, [&] {
                    ForgeSdkCapabilityV1 capability{};
                    capability.size = sizeof(capability);
                    check(h->query_capability(h->context, FORGE_SDK_ANIMATION, 1, &capability) ==
                                  1 &&
                              capability.available && capability.callable &&
                              (capability.flags & FORGE_SDK_FIXED_ONLY),
                          "Animation capability unavailable in PostPhysics");
                    capability.size = sizeof(capability);
                    check(h->query_capability(h->context, FORGE_SDK_ANIMATION, 2, &capability) ==
                                  1 &&
                              !capability.available && !capability.callable,
                          "Wrong capability version admitted");
                    for (uint32_t index = 0;; ++index) {
                        ForgeSdkAnimationEventV1 event{}, again{};
                        event.size = again.size = sizeof(event);
                        const auto result = h->animation_read_event(h->context, index, &event);
                        if (result == 0)
                            break;
                        check(result == 1 && event.version == 1 && event.native_entity &&
                                  event.tick && event.scene_uuid[0] && event.entity_uuid[0] &&
                                  event.states_uuid[0] && event.clip_uuid[0],
                              "Marker observation invalid");
                        check(h->animation_read_event(h->context, index, &again) == 1 &&
                                  event.marker == again.marker && event.tick == again.tick &&
                                  event.value == again.value &&
                                  std::strcmp(event.name, again.name) == 0,
                              "Broadcast read consumed marker");
                        char text[256];
                        std::snprintf(text, sizeof(text), "marker_probe: %u %llu %s %.6f %.6f",
                                      event.marker, static_cast<unsigned long long>(event.tick),
                                      event.name, event.offset, event.value);
                        h->diagnostic(h->context, 1, text);
                        if (event.marker == 4) {
                            // Structural changes use the callback stage and normal merge.
                            auto target = it.world().entity(event.native_entity);
                            check(target.is_alive(), "Marker target missing");
                            target.remove<forge::Animator>();
                            *removed = index;
                        }
                    }
                });
            })
            .add(h->fixed_tag);
        w.system()
            .kind(h->post_physics_phase)
            .read<forge::Animator>()
            .run([h, removed](flecs::iter&) {
                checked(h, [&] {
                    if (!*removed)
                        return;
                    ForgeSdkAnimationEventV1 event{};
                    event.size = sizeof(event);
                    check(h->animation_read_event(h->context, **removed, &event) == -1,
                          "Removed Animator target accepted after merge");
                    removed->reset();
                    h->diagnostic(h->context, 1, "marker_probe: removed target rejected");
                });
            })
            .add(h->fixed_tag);
        forge_register_system_MarkerPulse(h);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
const char* dependencies[] = {"forge.animation", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.marker_probe",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_ANIMATION | FORGE_SDK_DIAGNOSTICS,
                              FORGE_SDK_ANIMATION | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
