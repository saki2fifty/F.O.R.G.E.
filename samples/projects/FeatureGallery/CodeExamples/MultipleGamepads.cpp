// Original Gallery behavior. Each action reads one logical pad slot.
#include "ControllerSlots.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <flecs.h>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <span>
void forge_register_system_MultipleGamepads(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.system<forge::LocalTranslation>("gameplay.GalleryMultipleGamepads")
        .kind(host->fixed_phase)
        .each([host](flecs::iter& it, std::size_t row, forge::LocalTranslation& position) {
            constexpr std::array<const char*, 4> markers{
                "16700000-1670-4167-8167-000000000002", "16700000-1670-4167-8167-000000000003",
                "16700000-1670-4167-8167-000000000004", "16700000-1670-4167-8167-000000000005"};
            constexpr std::array<const char*, 4> actions{
                "16700000-1670-4167-8167-000000000010", "16700000-1670-4167-8167-000000000011",
                "16700000-1670-4167-8167-000000000012", "16700000-1670-4167-8167-000000000013"};
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 reference{};
            if (client.entity_reference(it.entity(row), reference) != 1)
                return;
            std::span<const char* const> marker_ids = markers, action_ids = actions;
            if (!std::strcmp(reference.scene_uuid, forge::examples::controller_slots_scene)) {
                marker_ids = forge::examples::controller_slot_markers;
                action_ids = forge::examples::controller_slot_actions;
            } else if (std::strcmp(reference.scene_uuid, "16700000-1670-4167-8167-167000000000")) {
                return;
            }
            for (std::size_t player = 0; player < marker_ids.size(); ++player) {
                if (std::strcmp(reference.entity_uuid, marker_ids[player]))
                    continue;
                ForgeSdkActionV1 move{};
                if (!client.action(action_ids[player], move))
                    return;
                const double distance = 3 * it.delta_time(); // Three world units per second.
                position.x = std::clamp(position.x + move.x * distance, -7., 7.);
                position.y = std::clamp(position.y + move.y * distance, -4., 4.);
                return;
            }
        })
        .add(host->fixed_tag);
}
