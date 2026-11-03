#include "render_texture_plan.hpp"
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation, const char* fragment) {
    try {
        operation();
    } catch (const std::exception& e) {
        check(std::string(e.what()).find(fragment) != std::string::npos,
              "Rejected pass did not explain the affected contract");
        return;
    }
    throw std::runtime_error("Unsafe camera output routing was accepted");
}
RenderCamera camera(AssetId target, int order = 0) {
    Camera value;
    value.render_texture = {target};
    value.order = order;
    return {EntityId::generate(), value, {}};
}
} // namespace
int main() {
    try {
        const auto a = AssetId::generate(), b = AssetId::generate(), c = AssetId::generate();
        RenderScene scene;
        scene.scene = AssetId::generate();
        scene.cameras = {camera(b, 2), camera(a), camera(b, -3), camera({})};
        scene.cameras[0].camera.viewport_x = .5;
        scene.cameras[0].camera.viewport_width = .5;
        scene.cameras[0].camera.clear_color = false;
        const std::map<AssetId, RenderTextureData> definitions{{a, {32, 16}}, {b, {80, 40}}};
        const std::map<EntityId, std::set<AssetId>> reads{{scene.cameras[0].entity, {a}}};
        const auto plan = prepare_render_texture_plan(scene, definitions, reads, {b});
        check(plan.order == std::vector<AssetId>{a, b} && plan.passes.size() == 2,
              "Same-frame image dependencies did not order producers first");
        const auto& composed = plan.passes.at(b).composition.cameras;
        check(composed.size() == 2 && composed[0].settings.order == -3 &&
                  composed[1].view.viewport == PixelViewport{40, 0, 40, 40} &&
                  !composed[1].settings.clear_color,
              "Shared output lost camera order, output extent or viewport clear intent");
        check(prepare_game_cameras(scene, 300, 200).cameras.size() == 1,
              "Offscreen camera was also composed into the main view");
        auto cyclic = reads;
        cyclic[scene.cameras[1].entity] = {b};
        rejects([&] { prepare_render_texture_plan(scene, definitions, cyclic); }, "feedback cycle");
        cyclic = reads;
        cyclic[scene.cameras[1].entity] = {a};
        rejects([&] { prepare_render_texture_plan(scene, definitions, cyclic); }, "feedback cycle");
        rejects([&] { prepare_render_texture_plan(scene, definitions, reads, {c}); },
                "no enabled camera writer");
        rejects([&] { prepare_render_texture_plan(scene, {{a, {32, 16}}}, reads); },
                "configuration is unavailable");
        auto disabled = scene;
        disabled.cameras[0].camera.enabled = disabled.cameras[2].camera.enabled = false;
        rejects([&] { prepare_render_texture_plan(disabled, definitions, {}, {b}); },
                "no enabled camera writer");
        auto too_large = definitions;
        too_large.at(a) = {2048, 2048};
        too_large.at(b) = {2048, 2048};
        rejects([&] { prepare_render_texture_plan(scene, too_large, reads); }, "pixel profile");
        auto invalid = scene;
        invalid.cameras[0].camera.viewport_width = 2;
        rejects([&] { prepare_render_texture_plan(invalid, definitions, reads); },
                "camera is invalid");
        auto too_many = scene;
        auto many_definitions = definitions;
        for (unsigned i = 0; i < 3; ++i) {
            const auto id = AssetId::generate();
            too_many.cameras.push_back(camera(id));
            many_definitions.emplace(id, RenderTextureData{16, 16});
        }
        rejects([&] { prepare_render_texture_plan(too_many, many_definitions, {}); },
                "four-output profile");
        too_many = scene;
        for (unsigned i = 0; i < 15; ++i)
            too_many.cameras.push_back(camera(a));
        rejects([&] { prepare_render_texture_plan(too_many, definitions, {}); },
                "16-camera profile");
        check(prepare_render_texture_plan(scene, definitions, reads, {b}).order == plan.order,
              "Rejected plans mutated reusable source selections");
        std::cout
            << "Camera output composition, bounds, producer order and feedback rejection passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
