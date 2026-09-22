#pragma once
#include "../src/authored_schema.hpp"
#include <forge/scene.hpp>
namespace forge::test {
// Trusted test declarations only. The actual editor still receives copied
// metadata through its ordinary isolated worker and publication path.
inline Json authored_capture_manifest(unsigned version) {
    struct PilotSettings {
        std::string callsign;
        double health;
        std::uint64_t lives;
        bool enabled;
    };
    EngineContext source(WorldRole::Validation);
    auto& world = source.world().world();
    const auto type = world.component<PilotSettings>()
                          .member<std::string>("callsign")
                          .member<double>("health")
                          .member<std::uint64_t>("lives")
                          .member<bool>("enabled");
    ecs_doc_set_name(world, type, "Pilot settings");
    ecs_doc_set_brief(world, type, "Example project values authored through native Flecs Meta.");
    detail::opt_in_authoring(world, type, "project.pilot", "project.capture", version,
                             {{"callsign", "Pathfinder"},
                              {"health", 75.5},
                              {"lives", UINT64_C(9007199254740993)},
                              {"enabled", true}},
                             "Gameplay");
    return {{"format", "forge.authored-types"},
            {"version", 1},
            {"profile", "shared-native-sdk"},
            {"fingerprint", std::string(64, 'a')},
            {"components", detail::export_authored_types(world)}};
}
} // namespace forge::test
