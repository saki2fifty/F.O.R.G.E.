#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "navigation_rebuild_access.hpp"
#include "runtime_package.hpp"
#include <iostream>
using namespace forge;
using namespace Diligent;
int main(int argc, char** argv) {
    try {
        test_gallery_capture::check(argc == 3 || argc == 4,
                                    "Need prepared Gallery, image scratch root and optional Scene");
        const std::string source_path =
            argc == 4 ? argv[3] : "Scenes/136-navigation-polygons.scene.json";
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_gallery_capture::check(device && context, "Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        Json source;
        std::ifstream(root / source_path) >> source;
        const auto id = source.at("asset_id").get<AssetId>();
        const bool rebuild = source_path == "Scenes/237-runtime-navigation-rebuild.scene.json";
        const auto worker = rebuild ? runtime_navigation_worker() : std::filesystem::path{};
        const auto ticks =
            rebuild ? std::array<unsigned, 3>{60, 180, 480} : std::array<unsigned, 3>{30, 60, 90};
        const auto boundary = [rebuild](RuntimeWorld& world, unsigned tick) {
            if (!rebuild || (tick != 90 && tick != 480))
                return;
            AssetRef<NavMeshAsset> asset;
            EntityRef agent;
            world.engine.world().world().each(
                [&](flecs::entity entity, const NavigationAgent& value) {
                    asset = value.navmesh;
                    agent = world.scene.reference(entity.get<PersistentEntityId>().value.str());
                });
            const auto navigation = world.engine.services().navigation();
            if (tick == 90) {
                const auto token = navigation->request_rebuild(asset);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                while (navigation->rebuild_state(token).phase <= NavigationRebuildPhase::Building) {
                    world.poll_navigation_rebuild();
                    test_gallery_capture::check(std::chrono::steady_clock::now() < deadline,
                                                "Visual navigation rebuild timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                test_gallery_capture::check(navigation->rebuild_state(token).phase ==
                                                NavigationRebuildPhase::Applied,
                                            "Visual navigation rebuild was not adopted");
            } else {
                const auto observed = navigation->agent_state(agent);
                test_gallery_capture::check(observed &&
                                                observed->phase == NavigationAgentPhase::Arrived,
                                            "Rendered Agent did not resume after rebaking");
            }
        };
        const auto authored =
            test_gallery_capture::render(presentation, context, root, id, scratch / "authored", 6,
                                         true, ticks, false, worker, boundary);
        const auto package = scratch / "Content";
        const std::array roots{id};
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated game", hidden = scratch / "SourcesOffline";
        std::filesystem::rename(package, moved);
        std::filesystem::rename(root, hidden);
        const auto exported =
            test_gallery_capture::render(presentation, context, moved, id, scratch / "relocated", 6,
                                         true, ticks, false, worker, boundary);
        test_gallery_capture::check(authored == exported, "Source-free Game displays changed");
        std::ofstream receipt(scratch / "receipt.json");
        receipt << Json{{"scene", id},
                        {"exact_display_pairs", 4},
                        {"source", source_path},
                        {"explicit_runtime_rebuild", rebuild},
                        {"scope", "Actual moving Game frames on software Vulkan; no "
                                  "Windows, physical GPU or graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        test_gallery_capture::check(bool(receipt), "Gallery Game receipt write failed");
        context->WaitForIdle();
        std::cout << "Gallery: four exact animated source-free relocated Game pairs passed: "
                  << scratch << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
