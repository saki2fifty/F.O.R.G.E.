#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_game_capture.hpp"
#include "game_export.hpp"
#include "game_presentation.hpp"
#include "render_texture_fixture.hpp"
#include "runtime_package.hpp"
#include "ui_filter_profile.hpp"
#include <forge/authoring.hpp>
#include <forge/project.hpp>
#include <forge/project_lease.hpp>
#include <forge/ui_assets.hpp>
#include <forge/ui_protocol.hpp>
#include <forge/ui_values.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
using namespace Diligent;
namespace {
using test_gallery_capture::check;
using test_gallery_capture::Pixels;
struct Fixture {
    Json source, schema;
    AssetId scene, output;
    EntityId panel;
    bool gallery = false;
};
Fixture author(const std::filesystem::path& root, bool filtered = false) {
    std::filesystem::create_directories(root / "Assets/UiOutputProbe");
    std::ofstream(root / "Assets/UiOutputProbe/Panel.rml")
        << R"rml(<rml><head><style>body {margin:0;width:100%;height:100%;background-color:#00ff00;}</style></head><body data-style-opacity="amount"></body></rml>)rml";
    std::ofstream(root / "Assets/UiOutputProbe/Main.rml")
        << R"rml(<rml><head><style>body {margin:0;} #marker {position:absolute;left:0;top:0;width:40px;height:20px;background-color:#ffff00;}</style></head><body><div id="marker"/></body></rml>)rml";
    if (filtered) {
        std::ofstream(root / "Assets/UiOutputProbe/Panel.rml")
            << R"rml(<rml><head><style>body {margin:0;width:100%;height:100%;background-color:#00ff00;backdrop-filter:blur(1px);decorator:radial-gradient(circle 120px,#008000,#00ff00);mask-image:linear-gradient(90deg,#000f,#ffff);filter:brightness(0.5) blur(1px) drop-shadow(#cc550080 4px 3px 2px);}</style></head><body data-style-opacity="amount"></body></rml>)rml";
        std::ofstream(root / "Assets/UiOutputProbe/Main.rml")
            << R"rml(<rml><head><style>body {margin:0;} #marker {position:absolute;left:0;top:0;width:40px;height:20px;background-color:#ffff00;backdrop-filter:brightness(0.8) blur(1px);decorator:conic-gradient(from 45deg,#ffff00,#e6c000);mask-image:radial-gradient(circle,#000f,#ffff);box-shadow:#20408080 4px 3px 4px 1px;filter:contrast(1) drop-shadow(#20408080 8px 6px 2px);}</style></head><body><div id="marker"/></body></rml>)rml";
    }
    const auto panel_asset = register_ui_document(root, "Assets/UiOutputProbe/Panel.rml");
    const auto main_asset = register_ui_document(root, "Assets/UiOutputProbe/Main.rml");
    auto catalog = AssetCatalog::open_project(root);
    AssetId output;
    for (const auto& [id, record] : catalog.records())
        if (record.source == "Assets/RenderTextures/Split.rendertexture.json")
            output = id;
    check(bool(output), "UI fixture output definition missing");
    Json source;
    std::ifstream(root / "Scenes/152-camera-render-textures.scene.json") >> source;
    EngineContext engine;
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto create = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"recipe", "empty"}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto panel = create("UI - output panel");
    scene.entity(panel)
        .set<UiDocument>({{panel_asset.id}, true, true, 0, {output}})
        .set<UiValues>({{{"amount", 1}}});
    scene.entity(create("UI - main marker")).set<UiDocument>({{main_asset.id}, true, true, 1});
    scene.save(root / "Scenes/152-camera-render-textures.scene.json");
    return {scene.document(), scene.schema(), scene.asset_id(), output, EntityId::parse(panel)};
}
void author_gallery(const std::filesystem::path& root, UiDiligentRenderer& renderer,
                    const std::vector<std::byte>& font) {
    const auto path = std::filesystem::path("Scenes/154-camera-output-ui.scene.json");
    const bool fresh = !std::filesystem::exists(root / path);
    const auto directory = root / "Assets/UI/CameraOutput";
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "Telemetry.rml") << R"rml(<rml><head><style>
body, div {display:block; font-family:Lato; color:#e7f4ff; pointer-events:none;}
body {margin:0; width:100%; height:100%;}
#panel {box-sizing:border-box; width:100%; height:82px; padding:12px 18px; background-color:#102438e8; border-bottom:2px #36e5a4;}
#title {font-size:22px; color:#67f1b9;} #detail {font-size:16px; margin-top:6px;}
</style></head><body><div id="panel" data-style-opacity="amount"><div id="title">CAMERA FEED / LIVE UI</div><div id="detail">Tick: {{tick}} | Paused: {{paused}}</div></div></body></rml>)rml";
    std::ofstream(directory / "Guide.rml") << R"rml(<rml><head><style>
body, div {display:block; font-family:Lato; color:#e7f4ff; pointer-events:none;}
body {margin:0; width:100%; height:100%;}
#title {position:absolute; left:3%; top:4%; font-size:22px; color:#67f1b9;}
#guide {box-sizing:border-box; position:absolute; left:3%; bottom:4%; width:94%; padding:10px; font-size:15px; background-color:#102438e8;}
</style></head><body><div id="title">Runtime UI in camera images</div><div id="guide">Left: camera output + UI. Right: same-frame relay.<br/>Pause and edit Telemetry / UI Values / amount to fade the panel.<br/>The main guide stays on the Game view. Display surfaces do not take pointer input.</div></body></rml>)rml";
    const auto telemetry = register_ui_document(root, "Assets/UI/CameraOutput/Telemetry.rml");
    const auto guide = register_ui_document(root, "Assets/UI/CameraOutput/Guide.rml");
    Json source;
    std::ifstream(root /
                  (fresh ? "Scenes/152-camera-render-textures.scene.json" : path_utf8(path))) >>
        source;
    // Reuse the existing two-monitor teaching arrangement and published resources,
    // with independent Scene/entity identities and newly authored UI documents.
    EngineContext engine;
    Scene scene(engine.world());
    if (fresh) {
        source["asset_id"] = scene.asset_id();
        for (auto& row : source["entities"])
            row["id"] = EntityId::generate();
    }
    scene.restore_snapshot(source);
    AssetId output;
    for (const auto& row : source.at("entities")) {
        const auto actor = scene.entity(row.at("id").get<std::string>());
        if (actor.has<Camera>() && actor.get<Camera>().render_texture.id &&
            actor.get<Camera>().layers == 1) {
            output = actor.get<Camera>().render_texture.id;
            break;
        }
    }
    check(bool(output), "Camera UI Gallery source output missing");
    const auto add = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"recipe", "empty"}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    if (fresh) {
        scene.entity(add("Telemetry - UI in Split camera output"))
            .set<UiDocument>({{telemetry.id}, true, true, 0, {output}})
            .set<UiValues>({{{"amount", 1}}});
        scene.entity(add("Guide - main Game view only"))
            .set<UiDocument>({{guide.id}, true, true, 1});
    }
    const auto lease = std::make_shared<ProjectLease>(root);
    auto catalog = AssetCatalog::open_project(root);
    const auto material = [&](const char* name) {
        const auto target = std::filesystem::path("Assets/UI/CameraOutput") /
                            (std::string(name) + ".material.json");
        for (const auto& [id, record] : catalog.records())
            if (record.source == target)
                return id;
        const auto base_path =
            std::filesystem::path("Assets/RenderTextures") / (std::string(name) + ".material.json");
        Json base;
        std::ifstream(root / base_path) >> base;
        auto binding = base.at("overrides").at("textures").at("baseColorTexture");
        // The immutable primitive's projected V grows with local Y. Author the
        // monitor's texture transform explicitly; do not flip a render backend.
        binding["slot"]["offset"] = {0, 1};
        binding["slot"]["scale"] = {1, -1};
        auto document = MaterialDocument::create(lease, target, [&](AssetId id) {
            auto source = MaterialSource::create(id);
            source.document["version"] = 3;
            source.document["base"] = AssetRef<MaterialAsset>{base.at("asset_id").get<AssetId>()};
            source.document["overrides"]["textures"]["baseColorTexture"] = binding;
            return source;
        });
        test_render_texture::publish(lease, *document, material_import_registry(),
                                     prepare_material_publication);
        return document->source().asset();
    };
    const auto split_material = material("Split"), relay_material = material("Relay");
    for (const auto& row : source.at("entities")) {
        auto actor = scene.entity(row.at("id").get<std::string>());
        if (actor.has<MeshRenderer>()) {
            auto mesh = actor.get<MeshRenderer>();
            if (mesh.layers == 4 || mesh.layers == 8) {
                mesh.materials.at(0).material = {mesh.layers == 4 ? split_material
                                                                  : relay_material};
                actor.set(mesh);
            }
        }
    }
    scene.save(root / path);
    catalog = AssetCatalog::open_project(root);
    const auto record = fresh ? catalog.add_scene(path) : catalog.records().at(scene.asset_id());
    catalog.save(AssetCatalog::project_index(root));
    RuntimeUiInspector inspector = [&](AssetId id, std::stop_token) {
        UiPresenter presenter(renderer.render_interface(), root, font);
        return presenter.inspect_static({id});
    };
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, inspector, scene.schema());
}
std::vector<Pixels> render(DiligentPresentation& presentation, IDeviceContext* context,
                           const std::filesystem::path& root, const Fixture& fixture,
                           const std::vector<std::byte>& font,
                           const std::filesystem::path& images) {
    std::filesystem::create_directories(images);
    GamePresentation visual(presentation, context, root, font);
    visual.dimensions(640, 360, 1);
    GameSessionConfig config;
    config.content_root = root;
    config.ui = true;
    config.preparation = [&](std::uint64_t ticket) { return visual.prepare(ticket); };
    GameSession game(std::move(config)); // Destroy session/candidates before the visual owner.
    const auto ticket = game.prepare(fixture.source);
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
        check(std::chrono::steady_clock::now() < deadline, "UI output scene preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    game.activate(ticket, RuntimeClock::Clock::now(), false);
    const auto tick = game.status().at("clock").at("tick");
    std::vector<Pixels> results;
    unsigned frame = 0;
    const auto capture = [&](unsigned width, unsigned height, const char* label) {
        visual.dimensions(width, height, 1);
        Pixels pixels;
        for (unsigned settle = 0; settle < 3; ++settle) {
            presentation.begin_frame();
            auto* image = visual.draw(game, double(++frame) / 60);
            check(image, "Standalone composition has no UI output display");
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            submit();
        }
        test_gallery_capture::ppm(images / (std::string(label) + ".ppm"), pixels, width, height);
        check(game.status().at("clock").at("tick") == tick,
              "UI presentation or model tuning advanced fixed gameplay");
        return pixels;
    };
    auto pixels = capture(640, 360, "output-visible");
    if (!fixture.gallery) {
        for (const auto x : {84u, 436u}) {
            const auto& green = pixels.at(180 * 640 + x);
            check(green[1] > green[0] + 40 && green[1] > green[2] + 40,
                  "UI output or same-frame relay did not reach its Mesh Material");
        }
        const auto marker = pixels.at(8 * 640 + 8), sky = pixels.at(8 * 640 + 320);
        check(marker[0] > 150 && marker[1] > 150 && marker[2] < 20 && sky[2] > sky[1],
              "UI main marker or output/main isolation differs");
    } else {
        // The original telemetry header is visible on both the direct image and
        // relay. Measure bright literal text in their upper image strips.
        for (unsigned left : {24u, 376u}) {
            unsigned bright = 0;
            for (unsigned y = 122; y < 155; ++y)
                for (unsigned x = left; x < left + 220; ++x) {
                    const auto pixel = pixels.at(y * 640 + x);
                    bright += pixel[1] > 150 && pixel[2] > 100;
                }
            check(bright > 45, "Gallery telemetry text missing from camera image or relay");
        }
    }
    check(visual.ui().output_targets() == std::vector<AssetId>{fixture.output},
          "Standalone presenter lost its saved output identity");
    const auto revision = visual.ui().presentation_revision();
    results.push_back(std::move(pixels));
    game.active().tune_property({fixture.scene, fixture.panel}, "forge.ui_values", "values",
                                Json::array({{{"name", "amount"}, {"value", 0.}}}));
    pixels = capture(640, 360, "output-transparent");
    const auto red = pixels.at(180 * 640 + 84), cyan = pixels.at(180 * 640 + 204);
    check(red[0] > red[1] + 40 && cyan[2] > cyan[0] + 40 && cyan[1] > cyan[0] + 40,
          "Paused UI model update did not restore current camera pixels");
    if (fixture.gallery) {
        unsigned changed = 0;
        for (unsigned y = 122; y < 155; ++y)
            for (unsigned x = 24; x < 250; ++x)
                changed += pixels.at(y * 640 + x) != results.front().at(y * 640 + x);
        check(changed > 200, "Gallery paused opacity did not change actual output pixels");
    }
    check(visual.ui().presentation_revision() == revision,
          "UI numeric update unnecessarily replaced its native contexts");
    results.push_back(std::move(pixels));
    game.active().tune_property({fixture.scene, fixture.panel}, "forge.ui_values", "values",
                                Json::array({{{"name", "amount"}, {"value", 1.}}}));
    results.push_back(capture(320, 180, "main-resized"));
    check(visual.ui().presentation_revision() == revision,
          "Main resize replaced fixed-output UI document contexts");
    return results;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || argc == 5, "Need Gallery, default font and UI output scratch root");
        if (std::string_view(argv[1]) == "--author") {
            const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
            EngineVkCreateInfo create;
            RefCntAutoPtr<IRenderDevice> device;
            RefCntAutoPtr<IDeviceContext> context;
            GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
            check(device && context, "Gallery UI inspection Vulkan device unavailable");
            UiDiligentRenderer renderer(device);
            author_gallery(std::filesystem::absolute(argv[2]), renderer, font);
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        const auto root = scratch / "Source";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const bool gallery = argc == 5 && std::string_view(argv[4]) == "--gallery";
        Fixture fixture;
        if (gallery) {
            std::ifstream(root / "Scenes/154-camera-output-ui.scene.json") >> fixture.source;
            EngineContext engine;
            Scene scene(engine.world());
            scene.restore_snapshot(fixture.source);
            fixture.schema = scene.schema();
            fixture.scene = scene.asset_id();
            fixture.gallery = true;
            for (const auto& row : fixture.source.at("entities")) {
                const auto actor = scene.entity(row.at("id").get<std::string>());
                if (actor.has<UiDocument>() && actor.get<UiDocument>().render_texture.id) {
                    fixture.panel = row.at("id").get<EntityId>();
                    fixture.output = actor.get<UiDocument>().render_texture.id;
                }
            }
            check(fixture.panel && fixture.output, "Gallery UI output assignment missing");
        } else {
            fixture = author(root, argc == 5 && std::string_view(argv[4]) == "--filters");
        }
        const auto font = asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        EngineVkCreateInfo create;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "UI output Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto original =
            render(presentation, context, root, fixture, font, scratch / "source-images");
        UiDiligentRenderer inspection_renderer(device);
        RuntimeUiInspector inspector = [&](AssetId id, std::stop_token) {
            UiPresenter presenter(inspection_renderer.render_interface(), root, font);
            return presenter.inspect_static({id});
        };
        const auto lease = std::make_shared<ProjectLease>(root);
        const std::array roots{fixture.scene};
        prepare_runtime_content_catalog(*lease, roots, inspector, fixture.schema);
        auto settings = ProjectSettings(root).document();
        settings["startup_scene"] = {
            {"asset", fixture.scene},
            {"source", gallery ? "Scenes/154-camera-output-ui.scene.json"
                               : "Scenes/152-camera-render-textures.scene.json"}};
        ProjectSettings(root).save(settings);
        // Synthetic kit exercises export/inventory/compatibility, not launch of
        // a platform executable. Rendering below uses the actual shared owners.
        const auto kit = scratch / "RuntimeKit";
        std::filesystem::create_directories(kit / "resources/ui");
        std::filesystem::copy_file(argv[2], kit / "resources/ui/LatoLatin-Regular.ttf");
        asset_storage::replace(kit / "forge_game.exe", "UI output inventory fixture");
        Json runtime{{"format", "forge.runtime-kit"},
                     {"version", 1},
                     {"executable", "forge_game.exe"},
                     {"target", {{"platform", "windows"}, {"backend", "d3d12"}}},
                     {"engine",
                      {{"profile", "static-core"},
                       {"sdk_fingerprint", std::string(64, 'a')},
                       {"source_commit", std::string(40, 'b')},
                       {"build_id", "local-ui-output-fixture"},
                       {"runtime_ui_protocol", ui_protocol::version},
                       {"runtime_ui_filters", ui_filter_detail::profile_version},
                       {"runtime_ui_gradients", 2}}},
                     {"files", Json::object()}};
        for (const auto& file : std::filesystem::recursive_directory_iterator(kit))
            if (file.is_regular_file()) {
                const auto bytes = asset_detail::read_bytes(file.path(), 4 * 1024 * 1024);
                runtime["files"][path_utf8(file.path().lexically_relative(kit))] = {
                    {"bytes", bytes.size()}, {"sha256", asset_detail::content_digest(bytes)}};
            }
        const auto write_kit = [&] {
            asset_storage::replace(kit / "forge.runtime-kit.json", runtime.dump());
        };
        write_kit();
        const auto destination = scratch / "Export";
        GameExportRequest request;
        request.destination = destination;
        request.runtime_kit = kit;
        request.reference_schema = fixture.schema;
        check(export_standalone_game(*lease, request, inspector).at("ok"),
              "Actual UI output export failed");
        const auto admitted =
            asset_detail::read_bytes(destination / "forge.standalone.json", 16 * 1024 * 1024);
        for (const Json& protocol : {Json(), Json(1), Json("2"), Json(2.0), Json(true), Json(3)}) {
            if (protocol.is_null())
                runtime["engine"].erase("runtime_ui_protocol");
            else
                runtime["engine"]["runtime_ui_protocol"] = protocol;
            write_kit();
            bool rejected = false;
            try {
                export_standalone_game(*lease, request, inspector);
            } catch (const std::exception& error) {
                rejected =
                    std::string(error.what()).find("UI image-output support") != std::string::npos;
            }
            check(rejected, "Incompatible UI output runtime kit was admitted");
            check(asset_detail::read_bytes(destination / "forge.standalone.json",
                                           16 * 1024 * 1024) == admitted,
                  "Rejected UI output kit replaced the last good export");
        }
        if (argc == 5 && std::string_view(argv[4]) == "--filters") {
            runtime["engine"]["runtime_ui_protocol"] = ui_protocol::version;
            for (const Json& profile : {Json(), Json(0), Json("1"), Json(1.0), Json(true), Json(1),
                                        Json(2), Json(3), Json(4), Json(6)}) {
                if (profile.is_null())
                    runtime["engine"].erase("runtime_ui_filters");
                else
                    runtime["engine"]["runtime_ui_filters"] = profile;
                write_kit();
                bool rejected = false;
                try {
                    export_standalone_game(*lease, request, inspector);
                } catch (const std::exception& error) {
                    rejected =
                        std::string(error.what()).find("UI filter support") != std::string::npos;
                }
                check(rejected, "Incompatible UI filter runtime kit was admitted");
                check(asset_detail::read_bytes(destination / "forge.standalone.json",
                                               16 * 1024 * 1024) == admitted,
                      "Rejected UI filter kit replaced the last good export");
            }
            runtime["engine"]["runtime_ui_filters"] = ui_filter_detail::profile_version;
            for (const Json& profile :
                 {Json(), Json(0), Json("1"), Json(1.0), Json(true), Json(1), Json(3)}) {
                if (profile.is_null())
                    runtime["engine"].erase("runtime_ui_gradients");
                else
                    runtime["engine"]["runtime_ui_gradients"] = profile;
                write_kit();
                bool rejected = false;
                try {
                    export_standalone_game(*lease, request, inspector);
                } catch (const std::exception& error) {
                    rejected =
                        std::string(error.what()).find("UI gradient support") != std::string::npos;
                }
                check(rejected, "Incompatible UI gradient runtime kit was admitted");
                check(asset_detail::read_bytes(destination / "forge.standalone.json",
                                               16 * 1024 * 1024) == admitted,
                      "Rejected UI gradient kit replaced the last good export");
            }
        }
        const auto moved = scratch / "Relocated game";
        std::filesystem::rename(destination, moved);
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto exported = render(presentation, context, moved / "content", fixture, font,
                                     scratch / "export-images");
        check(original == exported, "Relocated UI output display differs from its source");
        std::cout << "Actual standalone UI camera output, same-frame relay, paused model tuning, "
                     "main isolation/resize and relocated pixels passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
