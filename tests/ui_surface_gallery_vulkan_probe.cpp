#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "gallery_game_capture.hpp"
#include "game_presentation.hpp"
#include "runtime_package.hpp"
#include <RmlUi/Core.h>
#include <forge/ui_protocol.hpp>
#include <iostream>

using namespace forge;
using namespace Diligent;
using test_gallery_capture::check;
using test_gallery_capture::Pixels;
namespace {
Rml::Element* control(const char* tag, unsigned index) {
    for (int c = 0; c < Rml::GetNumContexts(); ++c)
        for (int d = 0; d < Rml::GetContext(c)->GetNumDocuments(); ++d) {
            Rml::ElementList elements;
            Rml::GetContext(c)->GetDocument(d)->GetElementsByTagName(elements, tag);
            if (elements.size() > index)
                return elements[index];
        }
    throw std::runtime_error("Gallery native control missing");
}
std::vector<Pixels> render(DiligentPresentation& presentation, IDeviceContext* context,
                           const std::filesystem::path& project, AssetId scene,
                           const std::vector<std::byte>& font,
                           const std::filesystem::path& images) {
    std::filesystem::create_directories(images);
    GamePresentation visual(presentation, context, project, font);
    visual.dimensions(640, 360, 1);
    GameSessionConfig config;
    config.content_root = project;
    config.ui = true;
    config.preparation = [&](std::uint64_t ticket) { return visual.prepare(ticket); };
    GameSession game(std::move(config));
    const auto ticket = game.prepare(load_game_scene(project, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    const auto submit = [&] {
        presentation.end_frame(context);
        context->Flush();
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    for (;;) {
        presentation.begin_frame();
        const auto state = game.poll_preparation(ticket);
        submit();
        if (state.ready)
            break;
        check(std::chrono::steady_clock::now() < deadline, "Surface Gallery preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    game.activate(ticket, RuntimeClock::Clock::now(), false);
    const auto initial_tick = game.status().at("clock").at("tick").get<unsigned>();
    check(game.status().at("state") == "paused", "Surface Gallery must start paused");
    unsigned frame = 0;
    const auto capture = [&](const char* name) {
        Pixels pixels;
        for (unsigned settle = 0; settle < 3; ++settle) {
            presentation.begin_frame();
            auto* image = visual.draw(game, double(++frame) / 60);
            check(image, "Surface Gallery output missing");
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            submit();
        }
        test_gallery_capture::ppm(images / (std::string(name) + ".ppm"), pixels, 640, 360);
        return pixels;
    };
    std::vector<Pixels> result;
    result.push_back(capture("surface-ready"));
    check(extract_render_scene(game.presentation()).ui_surfaces.size() == 1,
          "Runtime presentation omitted the authored interactive surface");
    // The Gallery's ordinary orthographic arrangement has an independently
    // known image rectangle: (24,120)..(264,240), sampling512x256 output pixels.
    const auto click = [&](Rml::Element* element) {
        const auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
        const int x = int(24 + 240 * (position.x + size.x * .5) / 512);
        const int y = int(120 + 120 * (position.y + size.y * .5) / 256);
        const bool moved = visual.ui().mouse_move(x, y, 0);
        const bool down = visual.ui().mouse_button(0, true, 0);
        const bool up = visual.ui().mouse_button(0, false, 0);
        if (!moved || !down || !up) {
            const bool native = !element->GetContext()->ProcessMouseMove(
                int(position.x + size.x * .5), int(position.y + size.y * .5), 0);
            throw std::runtime_error("Gallery control " + element->GetTagName() + " at " +
                                     std::to_string(x) + "," + std::to_string(y) + " consumed " +
                                     std::to_string(moved) + std::to_string(down) +
                                     std::to_string(up) + "; native=" + std::to_string(native) +
                                     ": " + visual.ui().diagnostic());
        }
    };
    click(control("button", 2));
    const auto request = visual.ui().pending_command();
    check(request && request->at("command") == "Step", "Surface Step command missing");
    ui_protocol::CommandGate commands;
    commands.reset(visual.session(), visual.generation());
    visual.ui().acknowledge(commands.dispatch(*request, [&](const Json& value) {
        auto service = std::static_pointer_cast<UiRuntime>(game.active().engine.services().ui());
        service->command(game.active().scene, value, [&](const std::string& name) {
            check(name == "Step", "Surface command crossed its native control");
            game.step();
        });
    }));
    check(game.status().at("clock").at("tick") == initial_tick + 1,
          "Surface Step did not reach fixed gameplay exactly once");
    result.push_back(capture("surface-stepped"));
    click(control("input", 0));
    check(visual.ui().wants_text() && visual.ui().text(" FORGE"),
          "Scene surface text field did not retain native text focus");
    check(control("input", 0)->GetAttribute<Rml::String>("value", "").find(" FORGE") !=
              Rml::String::npos,
          "Surface text dispatch did not update the native field value");
    result.push_back(capture("surface-text"));
    check(game.status().at("clock").at("tick") == initial_tick + 1,
          "Surface text or rendering advanced fixed gameplay");
    visual.ui().release_input();
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, default font and Gallery scratch root");
        const auto scratch = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        const auto root = scratch / "Source";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto catalog = AssetCatalog::open_project(root);
        AssetId scene;
        for (const auto& [id, row] : catalog.records())
            if (row.source == "Scenes/174-interactive-ui-surfaces.scene.json")
                scene = id;
        check(bool(scene), "Interactive surface Gallery is not registered");
        const auto font = asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        EngineVkCreateInfo create;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Surface Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto original =
            render(presentation, context, root, scene, font, scratch / "authored");
        const auto package = scratch / "Content";
        const std::array roots{scene};
        EngineContext definitions;
        Scene schema(definitions.world());
        UiDiligentRenderer inspection_renderer(device);
        const RuntimeUiInspector inspector = [&](AssetId id, std::stop_token) {
            UiPresenter ui(inspection_renderer.render_interface(), root, font);
            return ui.inspect_static({id});
        };
        package_runtime_content(root, package, roots, {"windows", "d3d12"}, {}, {}, schema.schema(),
                                inspector);
        const auto packaged = open_runtime_content(package, {"windows", "d3d12"});
        const auto& record = packaged.records().at(scene);
        check(record.metadata.at("forge.runtime_document").at("runtime_features") ==
                  Json{{"ui_surface_input", 1}},
              "Packaged surface Scene lost its runtime kit requirement");
        check(std::any_of(record.dependency_edges.begin(), record.dependency_edges.end(),
                          [](const auto& edge) {
                              return edge.expected_type == RenderTextureAsset::type &&
                                     edge.role == "document:forge.ui_surface_input.render_texture";
                          }),
              "Packaged surface lost its typed output reference");
        const auto moved = scratch / "Relocated game";
        std::filesystem::rename(package, moved);
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto exported =
            render(presentation, context, moved, scene, font, scratch / "relocated");
        check(original == exported, "Source-hidden relocated surface pixels or input differed");
        context->WaitForIdle();
        std::ofstream(scratch / "receipt.json")
            << Json{{"scene", scene},
                    {"exact_image_pairs", original.size()},
                    {"native_step", true},
                    {"native_text", true},
                    {"scope", "Production GamePresentation and source-hidden relocated content on "
                              "software Vulkan; no Windows or physical-device acceptance"}}
                   .dump(2)
            << '\n';
        std::cout << "Interactive surface Gallery native Step/text and three source-hidden "
                     "relocated Game image pairs passed: "
                  << scratch << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
