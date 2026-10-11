// Original copied contact-cache SDK proof; no solver headers or ownership.
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <stdexcept>
#include <thread>
namespace {
bool cleared(const ForgeSdkPhysicsContactV1& value) {
    return value.size == sizeof(value) && !value.begin && !value.tick &&
           !value.first_scene_uuid[0] && !value.first_entity_uuid[0] &&
           !value.second_scene_uuid[0] && !value.second_entity_uuid[0];
}
bool same(const ForgeSdkPhysicsContactV1& a, const ForgeSdkPhysicsContactV1& b) {
    return a.size == b.size && a.begin == b.begin && a.tick == b.tick &&
           !std::strcmp(a.first_scene_uuid, b.first_scene_uuid) &&
           !std::strcmp(a.first_entity_uuid, b.first_entity_uuid) &&
           !std::strcmp(a.second_scene_uuid, b.second_scene_uuid) &&
           !std::strcmp(a.second_entity_uuid, b.second_entity_uuid);
}
ForgeSdkPhysicsContactV1 dirty() {
    ForgeSdkPhysicsContactV1 value{};
    value.size = sizeof(value);
    value.begin = 42;
    value.tick = 42;
    std::strcpy(value.first_scene_uuid, "stale");
    std::strcpy(value.second_entity_uuid, "stale");
    return value;
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
            auto value = dirty();
            if (host->physics_contact_count(host->context) != 0 ||
                host->physics_contact_event(host->context, 0, &value) != 0 || !cleared(value))
                throw std::runtime_error("SDK replayed prior-tick contacts before physics");
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            const double velocity[] = {2, 0, 0};
            if (host->entity_reference(host->context, entity.id(), &ref) != 1 ||
                host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                           FORGE_SDK_PHYSICS_LINEAR_VELOCITY, velocity) != 1)
                throw std::runtime_error("Contact fixture cannot move its Dynamic target");
        })
        .add(host->fixed_tag);
    world.system<const forge::PhysicsBody>()
        .kind(host->post_physics_phase)
        .each([host](flecs::entity entity, const forge::PhysicsBody& body) {
            if (body.motion != 2)
                return;
            const auto count = host->physics_contact_count(host->context);
            if (count < 0 || count > 16384)
                throw std::runtime_error("SDK rejected valid fixed contact context");
            auto invalid = dirty();
            if (host->physics_contact_event(host->context, UINT32_MAX, &invalid) != 0 ||
                !cleared(invalid) || host->physics_contact_event(host->context, 0, nullptr) != -1)
                throw std::runtime_error("Contact index/null output guard failed");
            invalid.size = sizeof(invalid) - 1;
            invalid.begin = 42;
            if (host->physics_contact_event(host->context, 0, &invalid) != -1 ||
                invalid.begin != 42 || invalid.size != sizeof(invalid) - 1)
                throw std::runtime_error("SDK wrote an invalid-sized contact output");
            if (count) {
                bool rejected = false;
                std::thread foreign([&] {
                    auto value = dirty();
                    rejected = host->physics_contact_count(host->context) == -1 &&
                               host->physics_contact_event(host->context, 0, &value) == -1 &&
                               cleared(value);
                });
                foreign.join();
                if (!rejected)
                    throw std::runtime_error("SDK accepted foreign-thread contact observation");
            }
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            if (host->entity_reference(host->context, entity.id(), &ref) != 1)
                throw std::runtime_error("Contact actor identity missing");
            for (int32_t index = 0; index < count; ++index) {
                auto value = dirty(), again = dirty();
                if (host->physics_contact_event(host->context, index, &value) != 1 ||
                    host->physics_contact_event(host->context, index, &again) != 1 ||
                    !same(value, again) || !value.tick || value.begin > 1 ||
                    std::strcmp(value.first_scene_uuid, ref.scene_uuid) ||
                    std::strcmp(value.second_scene_uuid, ref.scene_uuid) ||
                    !std::strcmp(value.first_entity_uuid, value.second_entity_uuid) ||
                    (std::strcmp(value.first_entity_uuid, ref.entity_uuid) &&
                     std::strcmp(value.second_entity_uuid, ref.entity_uuid)))
                    throw std::runtime_error(
                        "SDK copied stale/consumed/incorrect contact identity");
                char message[96];
                std::snprintf(message, sizeof(message), "Contact cache %s | tick %llu",
                              value.begin ? "Begin" : "End", (unsigned long long)value.tick);
                if (host->diagnostic(host->context, 1, message) != 1)
                    throw std::runtime_error("Contact diagnostic marker failed");
            }
            if (host->physics_contact_count(host->context) != count)
                throw std::runtime_error("SDK changed contact slot count after reads");
        })
        .add(host->fixed_tag);
    return 1;
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    auto value = dirty();
    if (host->physics_contact_count(host->context) != -1 ||
        host->physics_contact_event(host->context, 0, &value) != -1 || !cleared(value)) {
        if (capacity)
            std::snprintf(error, capacity, "SDK accepted contact read outside fixed scope");
        return 0;
    }
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_contacts_probe",
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
