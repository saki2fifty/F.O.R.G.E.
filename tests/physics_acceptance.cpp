#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include <forge/game_session.hpp>
#include <forge/project_lease.hpp>
#ifdef FORGE_ENABLE_NATIVE_SDK
#include <forge/native_sdk.hpp>
#include <forge/sdk_client.hpp>
#endif
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json run_level(const std::filesystem::path& project, const Json& scene,
               const std::filesystem::path& sdk_library = {}) {
    GameSessionConfig config;
    config.content_root = project;
#ifdef FORGE_ENABLE_NATIVE_SDK
    if (!sdk_library.empty())
        config.modules.push_back(load_native_sdk(sdk_library, "project.sdk_probe", "1"));
#else
    require(sdk_library.empty(), "SDK probe requires a shared-SDK acceptance build");
#endif
    GameSession game(config);
    const auto start = std::chrono::steady_clock::now();
    const auto ticket = game.prepare(scene);
    while (!game.poll_preparation(ticket).ready) {
        require(std::chrono::steady_clock::now() - start < 30s,
                "Physics acceptance preparation timed out");
        std::this_thread::sleep_for(1ms);
    }
    game.activate(ticket, RuntimeClock::Time{}, false);
    auto& world = game.active();
    auto physics = world.physics();
    const auto ref = [&](const char* name) {
        for (const auto& row : scene.at("entities"))
            if (row.at("name") == name)
                return EntityRef{scene.at("asset_id").get<AssetId>(), row.at("id").get<EntityId>()};
        throw std::runtime_error(std::string("Missing fixture entity: ") + name);
    };
    const auto character = ref("Character"), platform = ref("Moving platform");
    require(physics->status().at("characters") == 1, "Acceptance character was not realized");
    require(physics->status().at("collision_resources").size() == 7,
            "Acceptance level missed collision families");
#ifdef FORGE_ENABLE_NATIVE_SDK
    if (!sdk_library.empty()) {
        const auto& w = world.engine.world().world();
        const auto* host = *static_cast<const ForgeSdkWorldV1* const*>(
            ecs_get_id(w.c_ptr(), w.lookup("sdk.host").id(), w.lookup("sdk.HostProbe").id()));
        sdk::Client client(host);
        const auto resources = physics->status().at("collision_resources");
        for (const auto& resource : resources) {
            const auto id = resource.at("asset").get<std::string>();
            const auto token = client.request_resource(FORGE_SDK_RESOURCE_COLLISION, id.c_str());
            require(token != 0, "SDK collision preload request rejected");
            physics->prepare_assets();
            ForgeSdkResourceV1 info{};
            require(client.inspect_resource(token, info) && info.retained_revision[0],
                    "SDK collision preload did not expose its usable revision");
            require(client.release_resource(token) && !client.inspect_resource(token, info),
                    "SDK collision observer was not revoked");
        }
    }
#endif
    physics->place_character(character, {4, .32, -3}, {}, true);
    for (unsigned i = 0; i < 30; ++i)
        game.step();
    require(physics->character(character).supporting_entity == platform,
            "Acceptance character did not settle on the moving platform");
    const auto ready = std::chrono::steady_clock::now();
    for (unsigned i = 1; i <= 120; ++i) {
        physics->move_kinematic(platform, {4 + i / 120., .15, -3}, {});
        game.step();
    }
    auto state = physics->character(character);
    require(state.supporting_entity == platform && std::abs(state.position.x - 5) < .1,
            "Acceptance platform did not carry the character");
    require(std::abs(state.ground_velocity[0] - .5) < .02,
            "Acceptance platform velocity was not exposed");
    const auto end = std::chrono::steady_clock::now();
    physics->jump_character(character, 4);
    physics->move_kinematic(platform, {4 + 121 / 120., .15, -3}, {});
    game.step();
    require(physics->character(character).jump_accepted, "Acceptance platform jump failed");
    const auto checkpoint = physics->checkpoint();
    Module module;
    RuntimeWorld recovered(module, {}, {}, {}, project, false);
    recovered.scene.restore_snapshot(world.scene.snapshot());
    const auto recovery_deadline = std::chrono::steady_clock::now() + 30s;
    while (!recovered.physics()->prepare_assets()) {
        require(std::chrono::steady_clock::now() < recovery_deadline,
                "Acceptance recovery timed out");
        std::this_thread::sleep_for(1ms);
    }
    recovered.physics()->restore(checkpoint);
    require(equivalent(recovered.physics()->character(character).position,
                       physics->character(character).position),
            "Acceptance recovery pose changed");
    const auto status = physics->status();
    game.unload(RuntimeClock::Time{});
    return {
        {"bodies", status.at("bodies")},
        {"characters", 1},
        {"collision_families", 7},
        {"preparation_and_settle_ms",
         std::chrono::duration<double, std::milli>(ready - start).count()},
        {"platform_ticks", 120},
        {"platform_simulation_ms", std::chrono::duration<double, std::milli>(end - ready).count()},
        {"jump_and_recovery", true}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4,
                "Usage: forge_physics_acceptance PROJECT EVIDENCE_DIRECTORY [SDK_PROBE]");
        const auto sdk = argc == 4 ? std::filesystem::absolute(std::filesystem::u8path(argv[3]))
                                   : std::filesystem::path{};
        const auto project = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
        const auto evidence = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
        std::filesystem::create_directories(evidence);
        const auto bytes = asset_detail::read_bytes(project / "main.scene.json", 8 * 1024 * 1024);
        const auto scene = Json::parse(bytes.begin(), bytes.end());
        const auto original = run_level(project, scene, sdk);
        {
            ProjectLease lease(project);
            auto catalog = AssetCatalog::open_project(project);
            const auto scene_id = scene.at("asset_id").get<AssetId>();
            if (!catalog.records().contains(scene_id))
                catalog.add_scene("main.scene.json");
            else
                require(catalog.records().at(scene_id).type == "scene" &&
                            catalog.records().at(scene_id).source == "main.scene.json",
                        "Acceptance scene identity has a conflicting catalog owner");
            catalog.save(AssetCatalog::project_index(project));
        }
        const auto package = evidence / "packaged", relocated = evidence / "relocated";
        const std::array roots{scene.at("asset_id").get<AssetId>()};
#ifdef _WIN32
        const RuntimePackageTarget target{"windows", "none"};
#else
        const RuntimePackageTarget target{"linux", "none"};
#endif
        package_runtime_content(project, package, roots, target);
        std::filesystem::rename(package, relocated);
        // Move the entire source tree out of reach before opening the packaged world.
        const auto unavailable = project.string() + "-unavailable";
        std::filesystem::rename(project, unavailable);
        struct Restore {
            std::filesystem::path source, destination;
            ~Restore() {
                std::error_code ec;
                std::filesystem::rename(source, destination, ec);
            }
        } restore{unavailable, project};
        const auto moved = run_level(relocated, scene, sdk);
        const auto catalog = AssetCatalog::open_project(relocated);
        for (const auto& [id, record] : catalog.records())
            require(record.type != "model" && record.type != "mesh",
                    "Collision-only scene retained its build-only render sources");
        const Json report{{"source", original},
                          {"source_free_relocated", moved},
                          {"render_source_required", false}};
        atomic_write(evidence / "physics-acceptance.json", report.dump(2));
        std::cout << report.dump(2) << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
