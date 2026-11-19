// Optional Gallery System; register this hook in project start with Navigation
// access. Runtime entity lookup is intrinsic. Scene237's floor moves once.
// Stop/replacement retires work through the existing navigation owner.
#include <cstring>
#include <flecs.h>
#include <forge/navigation_components.hpp>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <memory>

void forge_register_system_RuntimeNavigationRebuild(const ForgeSdkWorldV1* host) {
    struct State {
        uint64_t token = 0;
        bool finished = false;
    };
    auto state = std::make_shared<State>();
    flecs::world world(host->world);
    world.component<forge::NavigationAgent>("forge.navigation_agent");
    world.component<forge::NavigationSurface>("forge.navigation_surface");
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.system<const forge::NavigationAgent>("gameplay.RuntimeNavigationRebuild")
        .kind(host->fixed_phase)
        .each([host, state](flecs::entity actor, const forge::NavigationAgent& agent) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 ref{};
            if (state->finished || client.entity_reference(actor.id(), ref) != 1 ||
                std::strcmp(ref.scene_uuid, "21323e17-588a-4ce1-a0f5-04844ce6ab7c") != 0)
                return;
            if (!state->token) {
                bool ready = false;
                flecs::world current(host->world);
                current.each([&](const forge::NavigationSurface& surface,
                                 const forge::LocalTranslation& position) {
                    ready |= surface.enabled && position.x >= 2;
                });
                if (ready) {
                    const auto asset = agent.navmesh.id.str();
                    if (!client.navigation_rebuild_request(asset.c_str(), state->token)) {
                        client.diagnostic(2, "Runtime navigation request is unavailable or busy");
                        state->finished = true;
                    }
                }
                return;
            }
            ForgeSdkNavigationRebuildV1 observation{};
            if (!client.navigation_rebuild_state(state->token, observation)) {
                client.diagnostic(2, "Runtime navigation request was retired");
                state->finished = true;
                return;
            }
            if (observation.phase < FORGE_SDK_NAV_REBUILD_APPLIED)
                return;
            const bool applied = observation.phase == FORGE_SDK_NAV_REBUILD_APPLIED;
            client.diagnostic(applied ? 1 : 2, applied ? "Gallery runtime navigation rebuilt"
                                                       : observation.diagnostic);
            state->finished = true;
        })
        .add(host->fixed_tag);
}
