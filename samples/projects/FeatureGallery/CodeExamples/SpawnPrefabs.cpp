// Optional Gallery64 behavior. Compile this file in your project's C++ module.
#include <array>
#include <cmath>
#include <flecs.h>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <memory>
namespace {
constexpr const char* scene = "fab9f548-3f22-4fbc-a5e0-bd0a2cffd9a1";
constexpr const char* prefab = "3f95b972-aa2a-4fab-999a-8857a5a35e11";
struct Spawns {
    std::array<std::uint64_t, 3> requests{}, entities{};
    unsigned ticks = 0;
};
} // namespace
void forge_register_system_SpawnPrefabs(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.component<forge::LocalRotation>("forge.local_rotation");
    auto state = std::make_shared<Spawns>();
    world.system("gameplay.GalleryRuntimePrefabs")
        .kind(host->fixed_phase)
        .write<forge::LocalTranslation>()
        .write<forge::LocalRotation>()
        .run([host, state](flecs::iter& it) {
            const forge::sdk::Client client(host);
            ++state->ticks;
            auto request = [&](unsigned index, const char* name) {
                state->requests[index] = client.request_prefab(prefab, name, scene);
                if (!state->requests[index])
                    client.diagnostic(2, "Runtime Prefab request was rejected");
            };
            if (state->ticks == 1) {
                request(0, "Spawned left");
                request(1, "Spawned right");
            }
            if (state->ticks == 120)
                request(2, "Spawned later");
            for (unsigned index = 0; index < state->requests.size(); ++index) {
                auto& token = state->requests[index];
                if (token) {
                    ForgeSdkEntityV1 info{};
                    if (client.inspect_entity(token, info)) {
                        if (info.state == FORGE_SDK_ENTITY_READY) {
                            state->entities[index] = info.native_entity;
                            it.world()
                                .entity(info.native_entity)
                                .set<forge::LocalTranslation>({index == 0   ? -4.0
                                                               : index == 1 ? 4.0
                                                                            : 0.0,
                                                               1.3, index == 2 ? -3.0 : 0.0});
                        } else if (info.state == FORGE_SDK_ENTITY_PENDING)
                            continue;
                        else
                            client.diagnostic(2, info.diagnostic);
                    }
                    client.release_entity(token); // Observation only; keeps the group alive.
                    token = 0;
                }
                const auto id = state->entities[index];
                if (!id || !ecs_is_alive(it.world(), id))
                    continue;
                auto entity = it.world().entity(id);
                const auto q = entity.get<forge::LocalRotation>();
                const double half = (index == 1   ? -80
                                     : index == 2 ? 150
                                                  : 80) *
                                    it.delta_time() * .008726646259971648;
                const double s = std::sin(half), c = std::cos(half);
                forge::LocalRotation next{float(q.x * c - q.z * s), float(q.y * c + q.w * s),
                                          float(q.z * c + q.x * s), float(q.w * c - q.y * s)};
                const double length = std::sqrt(double(next.x) * next.x + double(next.y) * next.y +
                                                double(next.z) * next.z + double(next.w) * next.w);
                next.x /= length;
                next.y /= length;
                next.z /= length;
                next.w /= length;
                entity.set<forge::LocalRotation>(next);
            }
        })
        .add(host->fixed_tag);
}
