// Optional Gallery89 behavior. Uses ordinary authored transforms in its runtime copy.
#include "TouchGestures.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <flecs.h>
#include <forge/transform_components.hpp>
void forge_register_system_TouchGestures(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.component<forge::LocalRotation>("forge.local_rotation");
    world.component<forge::LocalScale>("forge.local_scale");
    world
        .system<forge::LocalTranslation, forge::LocalRotation, forge::LocalScale>(
            "gameplay.GalleryTouchGestures")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, forge::LocalTranslation& position,
                     forge::LocalRotation& rotation, forge::LocalScale& scale) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 reference{};
            if (client.entity_reference(entity, reference) != 1 ||
                std::strcmp(reference.scene_uuid, forge::examples::gesture_scene) ||
                std::strcmp(reference.entity_uuid, forge::examples::gesture_marker))
                return;
            forge::examples::TouchGestureSample sample;
            if (!forge::examples::read_touch_gesture(client, sample))
                return;
            // Relative deltas can belong to a gesture that ended before this tick;
            // do not discard them based on its current Active flag or multiply by dt.
            position.x = std::clamp(position.x + sample.pan_x * 16, -32., 32.);
            position.y = std::clamp(position.y - sample.pan_y * 10, -20., 20.);
            const double current = std::clamp(double(scale.x), .25, 4.);
            const auto next = float(std::exp(
                std::clamp(std::log(current) + sample.pinch, std::log(.25), std::log(4.))));
            scale = {next, next, next};
            const double angle = -sample.rotation / 2;
            const double sine = std::sin(angle), cosine = std::cos(angle);
            const double z = rotation.z * cosine + rotation.w * sine;
            const double w = rotation.w * cosine - rotation.z * sine;
            const double length = std::hypot(z, w);
            if (length > 0 && std::isfinite(length))
                rotation = {0, 0, float(z / length), float(w / length)};
        })
        .add(host->fixed_tag);
}
