#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
using test_tile_asset_io::check;
constexpr auto source = "Scenes/124-dynamic-tile-map-bodies.scene.json";
void verify_geometry(const std::filesystem::path& root, AssetId scene,
                     const std::vector<test_gallery_capture::Pixels>& captures) {
    check(captures.size() == 4, "Dynamic Gallery lost a requested display");
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Dynamic visual witness assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto document = world.scene.document();
    const auto verify = [&](unsigned index, unsigned width, unsigned height) {
        const auto& pixels = captures.at(index);
        const auto at = [&](Double2 p) {
            const double scale = double(height) / 14;
            const int x = int(std::floor(width / 2. + p[0] * scale));
            const int y = int(std::floor(height / 2. - p[1] * scale));
            check(x >= 0 && x < int(width) && y >= 0 && y < int(height),
                  "Dynamic visual witness is outside the Camera");
            return pixels.at(unsigned(y) * width + unsigned(x));
        };
        const auto background = at({0, 5.5});
        for (const auto& row : document.at("entities")) {
            if (!row.at("components").contains("forge.tile_map_renderer") ||
                (width < height && !row.at("name").get<std::string>().starts_with("Mirrored")))
                continue;
            const auto ref = world.scene.reference(row.at("id").get<std::string>());
            const auto state = world.physics()->body_state_2d(ref);
            check(bool(state), "Dynamic visual witness lost a body");
            const double sign = row.at("components").at("forge.local_scale").at("x");
            const auto point = [&](double x, double y) {
                x *= sign;
                return Double2{
                    state->position[0] + std::cos(state->angle) * x - std::sin(state->angle) * y,
                    state->position[1] + std::sin(state->angle) * x + std::cos(state->angle) * y};
            };
            const auto ring = at(point(.15, .75));
            const auto island = at(point(.75, .75));
            const auto hole = at(point(.45, .75));
            const auto bar = at(point(2.25, .15));
            const auto gap = at(point(2.25, .75));
            check(ring != background && island == ring && hole == background && bar != background &&
                      bar != ring && gap == background,
                  "Displayed moving ring/island/hole/flipped bars do not follow the rigid body");
        }
        check(at({0, -3.5}) != background, "Dynamic Gallery receiver was not displayed");
    };
    for (unsigned tick = 1; tick <= 90; ++tick) {
        world.simulation.tick(1.f / 60);
        if (tick == 30 || tick == 60 || tick == 90)
            verify(tick / 30 - 1, 640, 360);
    }
    verify(3, 360, 640);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_gallery_scene::prepare(argv[1], argv[2], argv[3], source);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Dynamic Map Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto captures = prepared.root.parent_path() / (prepared.scene.str() + "-images");
        const auto authored = test_gallery_capture::render(
            presentation, context, prepared.root, prepared.scene, captures / "authored", 4);
        verify_geometry(prepared.root, prepared.scene, authored);
        const auto output = prepared.root.parent_path() / "Content";
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / "Relocated content";
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported = test_gallery_capture::render(
            presentation, context, moved, prepared.scene, captures / "relocated", 4);
        verify_geometry(moved, prepared.scene, exported);
        check(authored == exported, "Relocated Dynamic Map Gallery displays differ");
        std::ofstream out(captures / "receipt.json");
        out << Json{{"scene", prepared.scene},
                    {"source", source},
                    {"exact_display_pairs", 4},
                    {"witnesses", "Moving ring/island/void/oriented bars/gap and receiver"},
                    {"scope", "Actual native Game on software Vulkan; no Windows/physical GPU or "
                              "graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        check(bool(out), "Dynamic Map Gallery visual receipt failed");
        context->WaitForIdle();
        std::cout << "Dynamic Map native Game rigid geometry witnesses and four exact source-free "
                     "displays PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
