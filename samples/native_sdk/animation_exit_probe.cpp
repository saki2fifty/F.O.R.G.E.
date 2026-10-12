// Matching public SDK: observe automatic state adoption and manually interrupt it.
#include <cstdio>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <stdexcept>
namespace {
struct ExitProbe {
    std::uint32_t ticks = 0, state = 0, marker_bits = 0, marker_state = 0, return_tick = 60;
    double time = 0, weight = 1;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        flecs::world w(h->world);
        w.component<forge::Animator>("forge.animator");
        const auto type = w.component<ExitProbe>("project.ExitProbe")
                              .member<std::uint32_t>("ticks")
                              .member<std::uint32_t>("state")
                              .member<std::uint32_t>("marker_bits")
                              .member<std::uint32_t>("marker_state")
                              .member<std::uint32_t>("return_tick")
                              .member<double>("time")
                              .member<double>("weight");
        return h->authoring_type(
            h->context, type, "project.exit_probe", 1,
            R"({"ticks":0,"state":0,"marker_bits":0,"marker_state":0,"return_tick":60,"time":0,"weight":1})",
            "Animation", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.system<forge::Animator, ExitProbe>("project.ExitControl")
        .kind(h->fixed_phase)
        .each([](forge::Animator& animator, ExitProbe& probe) {
            if (++probe.ticks == probe.return_tick)
                animator.state = 1;
        })
        .add(h->fixed_tag);
    w.system<const forge::Animator, ExitProbe>("project.ExitObservation")
        .kind(h->post_physics_phase)
        .each([h](flecs::entity entity, const forge::Animator& animator, ExitProbe& probe) {
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            ForgeSdkAnimationPlaybackV1 status{};
            status.size = sizeof(status);
            const auto reference_result = h->entity_reference(h->context, entity.id(), &ref);
            const auto playback_result =
                h->animation_playback(h->context, ref.scene_uuid, ref.entity_uuid, &status);
            if (reference_result != 1 || playback_result != 1) {
                std::fprintf(stderr, "Exit tick %u: reference %d, playback %d, entity %s\n",
                             probe.ticks, reference_result, playback_result, ref.entity_uuid);
                throw std::runtime_error("Timed exit copied playback is unavailable");
            }
            if (animator.state && status.state != animator.state) {
                std::fprintf(stderr, "Exit tick %u: typed state %u, copied state %u, time %.9g\n",
                             probe.ticks, animator.state, status.state, status.time);
                throw std::runtime_error("Automatic state differs from typed C++/copied status");
            }
            probe.state = status.state;
            probe.time = status.time;
            probe.weight = status.transition_weight;
            for (std::uint32_t i = 0; i < 4096; ++i) {
                ForgeSdkAnimationEventV1 event{};
                event.size = sizeof(event);
                const auto result = h->animation_read_event(h->context, i, &event);
                if (result == 0)
                    break;
                if (result != 1 || event.marker >= 32)
                    throw std::runtime_error("Invalid copied exit marker");
                if (event.native_entity == entity.id()) {
                    probe.marker_bits |= 1u << event.marker;
                    probe.marker_state = event.state;
                }
            }
        })
        .add(h->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.animation", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.animation_exit_probe",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_ANIMATION,
                              FORGE_SDK_ANIMATION,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
