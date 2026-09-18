// Small gameplay example: one Flecs authority, fixed input and optional capabilities.
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/navigation_components.hpp>
#include <forge/sdk_client.hpp>
#include <forge/ui_components.hpp>
namespace {
struct State {
    uint64_t ticks = 0, presses = 0, clicks = 0;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        flecs::world w(h->world); // Borrowed wrapper, never call fini.
        w.component<State>("example.State");
        w.component<forge::StableId>("forge.stable_id");
        w.component<forge::NavigationAgent>("forge.navigation_agent");
        w.component<forge::UiDocument>("forge.ui_document");
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        forge::sdk::Client client(h);
        if (!client.valid())
            return 0;
        if (client.available(forge::sdk::Capability::Ui) &&
            !client.allow_action("ExampleIncrement"))
            return 0;
        flecs::world w(h->world);
        w.entity("example.state").set<State>({});
        w.system<State>()
            .kind(h->fixed_phase)
            .each([h](State& state) {
                forge::sdk::Client api(h);
                forge::sdk::ProfileScope scope(api, "ExampleFixedTick");
                ++state.ticks;
                ForgeSdkActionV1 action{};
                if (api.action("12345678-1234-4234-8234-123456789abc", action))
                    state.presses += action.pressed;
                char entity[37]{};
                while (api.poll_action("ExampleIncrement", entity, sizeof(entity)) == 1)
                    ++state.clicks;
                const double origin[]{0, 10, 0}, ray[]{0, -20, 0};
                ForgeSdkPhysicsHitV1 hit{};
                if (api.callable(forge::sdk::Capability::Physics))
                    (void)api.raycast(origin, ray, hit); // Miss is normal, not an error.
                flecs::world world(h->world);
                world.each([&](const forge::NavigationAgent& agent) {
                    if (!agent.navmesh.id || !api.callable(forge::sdk::Capability::Navigation))
                        return;
                    double point[3]{};
                    ForgeSdkNavResultV1 result{sizeof(result), 0, 0, 1, point};
                    (void)api.navigation(agent.navmesh.id.str().c_str(), 0, origin, nullptr,
                                         result);
                    // A non-success NavStatus carries no usable points.
                });
                world.each([&](const forge::UiDocument&, const forge::StableId& id) {
                    if (api.callable(forge::sdk::Capability::Ui))
                        api.publish_number(id.value.c_str(), "example_ticks", double(state.ticks));
                });
                char message[160]{};
                std::snprintf(
                    message, sizeof(message),
                    "Example tick=%llu presses=%llu clicks=%llu physics=%u navigation=%u ui=%u",
                    (unsigned long long)state.ticks, (unsigned long long)state.presses,
                    (unsigned long long)state.clicks,
                    api.available(forge::sdk::Capability::Physics),
                    api.available(forge::sdk::Capability::Navigation),
                    api.available(forge::sdk::Capability::Ui));
                api.diagnostic(1, message);
            })
            .add(h->fixed_tag);
        // Exercise the same typed UUID support in an installed client.
        const auto id = forge::AssetId::generate();
        if (forge::AssetId::parse(id.str()) != id)
            return 0;
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        return 0;
    }
}
const char* dependencies[]{"forge.input", "forge.transforms"};
const ForgeNativeSdkV1 module{sizeof(module),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.example",
                              "1",
                              dependencies,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_DIAGNOSTICS,
                              FORGE_SDK_DIAGNOSTICS | FORGE_SDK_PROFILING | FORGE_SDK_PHYSICS |
                                  FORGE_SDK_AUDIO | FORGE_SDK_NAVIGATION | FORGE_SDK_UI,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &module;
}
