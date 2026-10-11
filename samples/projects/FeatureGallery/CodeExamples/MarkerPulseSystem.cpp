// Optional Gallery C++ System. Register this hook in your C++ project's start.
// The example scene's character roots have authored scale1. No private timers/state.
#include <algorithm>
#include <cstring>
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <forge/native_sdk.h>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
void forge_register_system_MarkerPulse(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::Animator>("forge.animator");
    world.component<forge::LocalScale>("forge.local_scale");
    world.system("gameplay.MarkerPulse")
        .kind(host->post_physics_phase)
        .read<forge::Animator>()
        .write<forge::LocalScale>()
        .run([host](flecs::iter& it) {
            const forge::sdk::Client client(host);
            for (uint32_t index = 0;; ++index) {
                ForgeSdkAnimationEventV1 event{};
                const auto status = client.animation_event(index, event);
                if (status == 0)
                    break;
                if (status < 0 || std::strcmp(event.name, "Pulse") != 0)
                    continue;
                auto entity = it.world().entity(event.native_entity);
                if (!entity.is_alive() || !entity.has<forge::Animator>())
                    continue;
                auto scale = entity.has<forge::LocalScale>() ? entity.get<forge::LocalScale>()
                                                             : forge::LocalScale{};
                scale.y = 1.f + .12f * float(std::clamp(event.value, 0., 1.));
                entity.set(scale);
            }
        })
        .add(host->fixed_tag);
}
