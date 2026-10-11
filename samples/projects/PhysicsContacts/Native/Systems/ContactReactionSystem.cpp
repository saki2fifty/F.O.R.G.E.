#include "../Components/ContactReaction.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace {
ForgeSdkEntityRefV1 reference(const ForgeSdkWorldV1* host, flecs::entity entity) {
    ForgeSdkEntityRefV1 ref{};
    ref.size = sizeof(ref);
    if (host->entity_reference(host->context, entity.id(), &ref) != 1)
        throw std::runtime_error("Contact example identity unavailable");
    return ref;
}
void velocity(const ForgeSdkWorldV1* host, const ForgeSdkEntityRefV1& ref, double speed) {
    const double vector[] = {speed, 0, 0};
    if (host->physics_set_velocity(host->context, ref.scene_uuid, ref.entity_uuid,
                                   FORGE_SDK_PHYSICS_LINEAR_VELOCITY, vector) != 1)
        throw std::runtime_error("Contact reaction velocity rejected");
}
bool includes(const ForgeSdkPhysicsContactV1& contact, const ForgeSdkEntityRefV1& ref) {
    return (!std::strcmp(contact.first_scene_uuid, ref.scene_uuid) &&
            !std::strcmp(contact.first_entity_uuid, ref.entity_uuid)) ||
           (!std::strcmp(contact.second_scene_uuid, ref.scene_uuid) &&
            !std::strcmp(contact.second_entity_uuid, ref.entity_uuid));
}
} // namespace
void forge_register_system_ContactReactionSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.system<ContactReaction>("gameplay.ContactLaunch")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, ContactReaction& data) {
            if (!std::isfinite(data.speed) || data.speed <= 0 || data.speed > 1e6)
                throw std::runtime_error("Contact example speed must be positive and bounded");
            if (!data.launched) {
                velocity(host, reference(host, entity), data.speed);
                data.launched = true;
            }
        })
        .add(host->fixed_tag);
    world.system<ContactReaction>("gameplay.ContactReactionSystem")
        .kind(host->post_physics_phase)
        .each([host](flecs::entity entity, ContactReaction& data) {
            const auto ref = reference(host, entity);
            const auto count = host->physics_contact_count(host->context);
            if (count < 0 || count > 16384)
                throw std::runtime_error("Contact example observation rejected");
            for (int32_t index = 0; index < count; ++index) {
                ForgeSdkPhysicsContactV1 event{};
                event.size = sizeof(event);
                const auto result = host->physics_contact_event(host->context, index, &event);
                if (result < 0)
                    throw std::runtime_error("Contact example copy rejected");
                if (!result || !includes(event, ref))
                    continue;
                auto& counter = event.begin ? data.begin_count : data.end_count;
                if (counter == std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("Contact example counter overflow");
                ++counter;
                if (event.begin && data.reverse_on_begin && !data.reacted) {
                    // PostPhysics writes queue intent for the next fixed boundary.
                    velocity(host, ref, -data.speed);
                    data.reacted = true;
                }
                char message[128];
                std::snprintf(message, sizeof(message), "Contact reaction: %s | tick %llu",
                              event.begin ? "Begin" : "End", (unsigned long long)event.tick);
                if (host->diagnostic(host->context, 1, message) != 1)
                    throw std::runtime_error("Contact example diagnostic rejected");
            }
        })
        .add(host->fixed_tag);
}
