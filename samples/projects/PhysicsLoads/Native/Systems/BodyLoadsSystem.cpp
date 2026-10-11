#include "../Components/BodyLoads.hpp"
#include <algorithm>
#include <cmath>
#include <forge/native_sdk_identity.h>
#include <forge/transform_components.hpp>
#include <stdexcept>

void forge_register_system_BodyLoadsSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.system<BodyLoads, const forge::LocalTranslation>("gameplay.BodyLoadsSystem")
        .kind(host->fixed_phase)
        .each([host](flecs::iter& it, size_t row, BodyLoads& data,
                     const forge::LocalTranslation& position) {
            if (data.kind > FORGE_SDK_PHYSICS_ANGULAR_IMPULSE || !std::isfinite(data.duration) ||
                data.duration < 0 || !std::isfinite(data.elapsed) || data.elapsed < 0)
                throw std::runtime_error("Invalid Body Loads example settings");
            const bool continuous =
                data.kind == FORGE_SDK_PHYSICS_FORCE || data.kind == FORGE_SDK_PHYSICS_TORQUE;
            const double dt = it.delta_time();
            if (continuous ? data.elapsed < data.duration : !data.triggered) {
                const double duty =
                    continuous ? std::min(dt, data.duration - data.elapsed) / dt : 1;
                const double vector[] = {data.x * duty, data.y * duty, data.z * duty};
                const double point[] = {position.x + data.offset_x, position.y + data.offset_y,
                                        position.z + data.offset_z};
                ForgeSdkEntityRefV1 ref{};
                ref.size = sizeof(ref);
                if (host->entity_reference(host->context, it.entity(row).id(), &ref) != 1 ||
                    host->physics_apply(host->context, ref.scene_uuid, ref.entity_uuid, data.kind,
                                        vector, data.at_point ? point : nullptr) != 1)
                    throw std::runtime_error("Body Loads example rejected its physics command");
                data.triggered = true;
            }
            data.elapsed += dt;
        })
        .add(host->fixed_tag);
}
