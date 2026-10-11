#include "../Components/VolumeProbe.hpp"
#include <array>
#include <stdexcept>
void forge_register_system_VolumeProbeSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.system<VolumeProbe>("gameplay.VolumeLaunch")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, VolumeProbe& data) {
            if (data.launched)
                return;
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            const double velocity[] = {data.speed, 0, 0};
            if (host->entity_reference(host->context, entity.id(), &ref) != 1 ||
                host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                           FORGE_SDK_PHYSICS_LINEAR_VELOCITY, velocity) != 1)
                throw std::runtime_error("Volume actor launch rejected");
            data.launched = true;
        })
        .add(host->fixed_tag);
    world.system<VolumeProbe>("gameplay.VolumeObserve")
        .kind(host->post_physics_phase)
        .each([host](VolumeProbe& data) {
            ForgeSdkOverlapV1 query{};
            query.size = sizeof(query);
            query.shape = 1; // Sphere: radius in dimensions[0].
            query.layer_mask = UINT32_MAX;
            query.include_sensors = data.include_sensors;
            query.dimensions[0] = data.radius;
            query.origin[0] = data.center_x;
            query.origin[1] = data.center_y;
            query.origin[2] = data.center_z;
            query.rotation[3] = 1;
            std::array<ForgeSdkEntityRefV1, FORGE_SDK_PHYSICS_OVERLAP_LIMIT> hits{};
            for (auto& hit : hits)
                hit.size = sizeof(hit);
            const auto count =
                host->physics_overlap_shape(host->context, &query, hits.data(), hits.size());
            if (count < 0)
                throw std::runtime_error("Volume query invalid or result budget exceeded");
            if (!data.observed || data.last_count != std::uint32_t(count)) {
                char text[96];
                std::snprintf(text, sizeof(text), "Volume query: count %d", count);
                if (host->diagnostic(host->context, 1, text) != 1)
                    throw std::runtime_error("Volume query diagnostic rejected");
            }
            data.observed = true;
            data.last_count = count;
        })
        .add(host->fixed_tag);
}
