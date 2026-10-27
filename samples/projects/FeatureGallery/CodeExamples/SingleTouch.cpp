// Original Gallery91 behavior. Recognition pulses already belong to this fixed tick.
#include "TouchGestures.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <flecs.h>
#include <forge/transform_components.hpp>
void forge_register_system_SingleTouch(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.component<forge::LocalRotation>("forge.local_rotation");
    world.component<forge::LocalScale>("forge.local_scale");
    world
        .system<forge::LocalTranslation, forge::LocalRotation, forge::LocalScale>(
            "gameplay.GallerySingleTouch")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, forge::LocalTranslation& position,
                     forge::LocalRotation& rotation, forge::LocalScale& scale) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 reference{};
            if (client.entity_reference(entity, reference) != 1 ||
                std::strcmp(reference.scene_uuid, forge::examples::single_touch_scene) ||
                std::strcmp(reference.entity_uuid, forge::examples::single_touch_marker))
                return;
            std::array<ForgeSdkActionV1, 7> values{};
            for (unsigned i = 0; i < values.size(); ++i)
                if (!client.action(forge::examples::single_touch_actions[i], values[i]))
                    return;
            if (values[1].pressed) { // Double tap also supplies Tap; reset takes precedence.
                position = {};
                rotation = {};
                scale = {1, 1, 1};
                return;
            }
            if (values[0].pressed) {
                const double z = double(rotation.z) + rotation.w;
                const double w = double(rotation.w) - rotation.z;
                const double length = std::hypot(z, w);
                if (length > 0 && std::isfinite(length))
                    rotation = {0, 0, float(z / length), float(w / length)};
            }
            if (values[2].pressed) {
                const float next = scale.x >= 1 ? .5f : 1.f;
                scale = {next, next, next};
            }
            position.x = std::clamp(position.x + double(values[4].pressed != 0) -
                                        double(values[3].pressed != 0),
                                    -6., 6.);
            position.y = std::clamp(position.y + double(values[5].pressed != 0) -
                                        double(values[6].pressed != 0),
                                    -3., 3.);
        })
        .add(host->fixed_tag);
}
