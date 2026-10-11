// Original exact-SDK volume proof. Uses copied UUIDs, no solver handles.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <stdexcept>
#include <thread>
namespace {
ForgeSdkOverlapV1 query() {
    ForgeSdkOverlapV1 value{};
    value.size = sizeof(value);
    value.shape = 1;
    value.layer_mask = UINT32_MAX;
    value.include_sensors = 1;
    value.dimensions[0] = 1;
    value.rotation[3] = 1;
    return value;
}
ForgeSdkEntityRefV1 dirty() {
    ForgeSdkEntityRefV1 ref{};
    ref.size = sizeof(ref);
    ref.version = 42;
    std::strcpy(ref.scene_uuid, "stale");
    std::strcpy(ref.entity_uuid, "stale");
    return ref;
}
bool cleared(const ForgeSdkEntityRefV1& ref) {
    return ref.size == sizeof(ref) && !ref.version && !ref.scene_uuid[0] && !ref.entity_uuid[0];
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world).component<forge::PhysicsBody>("forge.physics_body");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<const forge::PhysicsBody>()
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody& body) {
            if (body.motion != 2)
                return;
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            const double velocity[] = {1, 0, 0};
            if (host->entity_reference(host->context, entity.id(), &ref) != 1 ||
                host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                           FORGE_SDK_PHYSICS_LINEAR_VELOCITY, velocity) != 1)
                throw std::runtime_error("Volume probe cannot move its target");
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsBody>()
        .kind(host->post_physics_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody& body) {
            if (body.motion != 2)
                return;
            auto request = query();
            std::array<ForgeSdkEntityRefV1, FORGE_SDK_PHYSICS_OVERLAP_LIMIT> hits;
            hits.fill(dirty());
            const auto count =
                host->physics_overlap_shape(host->context, &request, hits.data(), hits.size());
            ForgeSdkEntityRefV1 actor{};
            actor.size = sizeof(actor);
            ForgeSdkPhysicsBodyStateV1 state{};
            state.size = sizeof(state);
            if (host->entity_reference(host->context, entity.id(), &actor) != 1 ||
                host->physics_body_state(host->context, actor.scene_uuid, actor.entity_uuid,
                                         &state) != 1 ||
                count < 2 || count > 3)
                throw std::runtime_error("Volume probe missing realized native targets");
            const auto distance = std::abs(state.centre_of_mass[0]);
            if ((distance < 1.49 && count != 3) || (distance > 1.51 && count != 2))
                throw std::runtime_error(
                    "Volume sphere/box intersection disagrees with known geometry");
            bool includes_actor = false;
            for (int32_t i = 0; i < count; ++i) {
                if (hits[i].size != sizeof(hits[i]) || hits[i].version != 1 ||
                    std::strlen(hits[i].entity_uuid) != 36 ||
                    std::strcmp(hits[i].scene_uuid, actor.scene_uuid) ||
                    (i && std::strcmp(hits[i - 1].entity_uuid, hits[i].entity_uuid) >= 0))
                    throw std::runtime_error("Volume UUID copies lost canonical sorted uniqueness");
                includes_actor |= !std::strcmp(hits[i].entity_uuid, actor.entity_uuid);
            }
            if (includes_actor != (count == 3))
                throw std::runtime_error("Volume result contains an incorrect sensor identity");
            for (std::size_t i = count; i < hits.size(); ++i)
                if (!cleared(hits[i]))
                    throw std::runtime_error("Volume left stale unused output slots");
            std::array<ForgeSdkEntityRefV1, FORGE_SDK_PHYSICS_OVERLAP_LIMIT> again;
            again.fill(dirty());
            if (host->physics_overlap_shape(host->context, &request, again.data(), again.size()) !=
                count)
                throw std::runtime_error("Volume repeated read changed result count");
            for (int32_t i = 0; i < count; ++i)
                if (std::strcmp(hits[i].scene_uuid, again[i].scene_uuid) ||
                    std::strcmp(hits[i].entity_uuid, again[i].entity_uuid))
                    throw std::runtime_error("Volume repeated read changed identities");
            auto small = dirty();
            if (host->physics_overlap_shape(host->context, &request, &small, 1) != -2 ||
                !cleared(small))
                throw std::runtime_error("Volume returned partial capacity success");
            request.include_sensors = 0;
            if (host->physics_overlap_shape(host->context, &request, again.data(), again.size()) !=
                2)
                throw std::runtime_error("Volume sensor filtering failed");
            request.layer_mask = 0;
            if (host->physics_overlap_shape(host->context, &request, again.data(), again.size()) !=
                0)
                throw std::runtime_error("Volume layer filtering failed");
            for (const auto& value : again)
                if (!cleared(value))
                    throw std::runtime_error("Volume miss retained old identities");
            request = query();
            auto invalid = dirty();
            if (host->physics_overlap_shape(host->context, nullptr, &invalid, 1) != -1 ||
                !cleared(invalid) ||
                host->physics_overlap_shape(host->context, &request, nullptr, 1) != -1)
                throw std::runtime_error("Volume null guard failed");
            invalid = dirty();
            if (host->physics_overlap_shape(host->context, &request, &invalid, UINT32_MAX) != -1 ||
                invalid.version != 42)
                throw std::runtime_error("Volume capacity guard wrote beyond valid storage");
            if (host->physics_overlap_shape(host->context, &request, &invalid, 0) != -1 ||
                invalid.version != 42)
                throw std::runtime_error("Volume zero capacity changed output");
            std::array<ForgeSdkEntityRefV1, 2> bad;
            bad.fill(dirty());
            --bad[1].size;
            if (host->physics_overlap_shape(host->context, &request, bad.data(), bad.size()) !=
                    -1 ||
                bad[0].version != 42 || bad[1].version != 42)
                throw std::runtime_error("Volume invalid output array partially written");
            request.dimensions[0] = NAN;
            if (host->physics_overlap_shape(host->context, &request, &invalid, 1) != -1 ||
                !cleared(invalid))
                throw std::runtime_error("Volume nonfinite request accepted");
            for (unsigned mode = 0; mode < 4; ++mode) {
                request = query();
                if (mode == 0)
                    --request.size;
                else if (mode == 1)
                    request.include_sensors = 2;
                else if (mode == 2)
                    request.shape = 4;
                else
                    request.rotation[3] = 0;
                invalid = dirty();
                if (host->physics_overlap_shape(host->context, &request, &invalid, 1) != -1 ||
                    !cleared(invalid))
                    throw std::runtime_error("Volume accepted invalid request admission");
            }
            if (std::abs(state.centre_of_mass[0] + 2.5) < .005) {
                bool rejected = false;
                std::thread foreign([&] {
                    auto value = dirty();
                    const auto req = query();
                    rejected = host->physics_overlap_shape(host->context, &req, &value, 1) == -1 &&
                               cleared(value);
                });
                foreign.join();
                if (!rejected)
                    throw std::runtime_error("Volume accepted foreign owner");
            }
            char text[192];
            std::snprintf(text, sizeof(text), "Volume probe: count %d | sensor %u", count,
                          unsigned(includes_actor));
            if (host->diagnostic(host->context, 1, text) != 1)
                throw std::runtime_error("Volume diagnostic marker rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    auto value = dirty();
    const auto req = query();
    if (host->physics_overlap_shape(host->context, &req, &value, 1) != -1 || !cleared(value)) {
        if (capacity)
            std::snprintf(error, capacity, "Volume accepted nonfixed scope");
        return 0;
    }
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_overlap_probe",
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_PHYSICS,
                              FORGE_SDK_PHYSICS | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              ready};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
