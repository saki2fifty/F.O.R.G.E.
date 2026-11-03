// Original Gallery observer. Input Message sends a copied LocalTranslation.
#include <cstring>
#include <flecs.h>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>

void forge_register_system_InputMessages(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.observer("gameplay.GalleryInputMessages")
        .event<forge::LocalTranslation>()
        .with(flecs::Any)
        .each([host](flecs::iter& it, std::size_t row) {
            const auto* payload = it.param<forge::LocalTranslation>();
            if (!payload)
                return;
            ForgeSdkEntityRefV1 source{};
            const forge::sdk::Client client(host);
            if (client.entity_reference(it.entity(row), source) != 1 ||
                std::strcmp(source.scene_uuid, "8d2fcdf4-9786-482f-b8b2-058d4168f1d3"))
                return;
            auto next = *payload;
            next.x = next.x >= 3.5 ? -3. : next.x + .5;
            it.entity(row).set<forge::LocalTranslation>(next);
        });
}
