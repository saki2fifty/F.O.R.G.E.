// Typed payloads and current fixed input share the same runtime callback scope.
#include "InputMessageTriggers.hpp"
#include <cstring>
#include <flecs.h>
#include <forge/input_message.hpp>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>

void forge_register_system_InputMessageTriggers(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.observer("gameplay.GalleryInputMessageTriggers")
        .event<forge::LocalTranslation>()
        .with(flecs::Any)
        .each([host](flecs::iter& it, std::size_t row) {
            const auto* payload = it.param<forge::LocalTranslation>();
            if (!payload)
                return;
            ForgeSdkEntityRefV1 source{};
            const forge::sdk::Client client(host);
            if (client.entity_reference(it.entity(row), source) != 1 ||
                std::strcmp(source.scene_uuid, gallery_input_triggers::scene))
                return;
            const auto entity = it.entity(row);
            if (!entity.has<forge::InputMessage>())
                return;
            const auto& config = entity.get<forge::InputMessage>();
            ForgeSdkActionV1 input{};
            if (!client.action(config.action.c_str(), input))
                return;
            auto next = *payload;
            if (config.trigger == std::uint32_t(forge::InputMessageTrigger::Released))
                next.y = next.y > 1 ? .5 : 2;
            else if (config.action == gallery_input_triggers::axis1)
                next.y = 1.5 + 2 * input.x;
            else if (config.action == gallery_input_triggers::axis2) {
                next.x = 1.5 + 2 * input.x;
                next.y = 1.5 + 2 * input.y;
            } else
                return;
            entity.set<forge::LocalTranslation>(next);
        });
}
