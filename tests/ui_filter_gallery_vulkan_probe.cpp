#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_game_capture.hpp"
#include "game_presentation.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_package.hpp"
#include <RmlUi/Core.h>
#include <forge/authoring.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/project_lease.hpp>
#include <forge/ui_assets.hpp>
#include <forge/ui_values.hpp>
#include <iostream>

using namespace forge;
using namespace Diligent;
namespace {
using test_gallery_capture::check;
using test_gallery_capture::Pixels;
const char* scene_path = "Scenes/155-ui-filters.scene.json";
bool shadows = false, backdrops = false, gradient_gallery = false, curved_gallery = false,
     masks_gallery = false;
const char* parameter = "blur";
const char* ui_path = "Assets/UI/Filters/Lab.rml";
Json read_scene(const std::filesystem::path& root) {
    const auto bytes = asset_detail::read_bytes(root / scene_path, 4 * 1024 * 1024);
    return Json::parse(bytes.begin(), bytes.end());
}
RuntimeUiInspector inspector(UiDiligentRenderer& renderer, const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [&](AssetId id, std::stop_token) {
        UiPresenter presenter(renderer.render_interface(), root, font);
        return presenter.inspect_static({id});
    };
}
void author(const std::filesystem::path& root, UiDiligentRenderer& renderer,
            const std::vector<std::byte>& font) {
    const bool fresh = !std::filesystem::exists(root / scene_path);
    std::filesystem::create_directories((root / ui_path).parent_path());
    asset_storage::replace(root / ui_path, masks_gallery      ? R"rml(<rml><head><style>
body,div,p,h1 {display:block;} body {margin:0;width:100%;height:100%;font-family:Lato;font-size:15px;color:#e5f2ff;pointer-events:none;}
#lab {position:absolute;left:4%;top:2%;width:92%;}
h1 {font-size:23px;margin:0 0 3px;color:#79e7cf;} p {margin:3px 0;}
#grid {margin-top:6px;} .card {display:inline-block;width:30%;margin:0 2% 6px 0;vertical-align:top;}
.label {height:19px;font-size:13px;} .sample {position:relative;height:56px;border-radius:6px;background-color:#e96644;}
.word {position:absolute;left:18%;top:20px;font-size:14px;color:white;}
#linear {mask-image:linear-gradient(90deg,#0000,#ffff);}
#radial {mask-image:radial-gradient(circle closest-side,#000f 45%,#0000 100%);}
#conic {mask-image:repeating-conic-gradient(from 45deg,#000f 0deg,#0000 90deg);}
#image {mask-image:image("../BitmapFormats/health.png" contain);}
#motion {width:100px;height:12px;margin-top:4px;background-color:#79e7cf;animation:2s linear-in infinite alternate reveal;}
@keyframes reveal {from {mask-image:linear-gradient(0deg,#0000,#ffff);} to {mask-image:linear-gradient(180deg,#0000,#ffff);}}
button {display:inline-block;padding:5px 8px;margin:4px 5px 0 0;background-color:#30536e;pointer-events:auto;}
button:hover,button:focus {background-color:#4c849e;} .hint {font-size:13px;color:#adc7db;}
</style></head><body><div id="lab"><h1>UI alpha masks</h1><p>Alpha controls the surface and its contents.</p><div id="grid">
<div class="card"><div class="label">Reference</div><div class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Linear fade</div><div id="linear" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Radial fade</div><div id="radial" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Conic fade</div><div id="conic" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">PNG alpha</div><div id="image" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Live / {{angle}}</div><div class="sample" data-style-mask-image="'linear-gradient(' + angle + 'deg,#0000,#ffff)'"><div class="word">UI</div></div></div>
</div><p class="hint">UI Values / angle: 0..360. Tick: {{tick}}</p><div id="motion"/>
<button id="pause" data-event-click="command('Pause')">Pause</button><button id="resume" data-event-click="command('Resume')">Resume</button><button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml"
                                           : curved_gallery   ? R"rml(<rml><head><style>
body,div,p,h1 {display:block;} body {margin:0;width:100%;height:100%;font-family:Lato;font-size:15px;color:#e5f2ff;pointer-events:none;}
#lab {position:absolute;left:4%;top:2%;width:92%;}
h1 {font-size:23px;margin:0 0 3px;color:#79e7cf;} p {margin:3px 0;}
#grid {margin-top:6px;} .card {display:inline-block;width:30%;margin:0 2% 6px 0;vertical-align:top;}
.label {height:19px;font-size:13px;} .sample {position:relative;height:56px;border-radius:6px;background-color:#1b3347;}
.word {position:absolute;left:18%;top:20px;font-size:14px;color:white;}
#circle {decorator:radial-gradient(circle 48px,#79e7cf,#4e96df);}
#ellipse {decorator:radial-gradient(70px 24px at 35% 50%,#e96644,#4e96df);}
#rings {decorator:repeating-radial-gradient(circle 24px,#e96644 0%,#4e96df 50%);}
#conic {decorator:conic-gradient(#e96644,#79e7cf,#4e96df,#e96644);}
#wedges {decorator:repeating-conic-gradient(from 45deg,#e9664480 0deg,#4e96df 90deg);}
#motion {width:100px;height:12px;margin-top:4px;animation:2s linear-in infinite alternate turn;}
@keyframes turn {from {decorator:conic-gradient(from 0deg,#e96644,#4e96df);} to {decorator:conic-gradient(from 180deg,#e96644,#4e96df);}}
button {display:inline-block;padding:5px 8px;margin:4px 5px 0 0;background-color:#30536e;pointer-events:auto;}
button:hover,button:focus {background-color:#4c849e;} .hint {font-size:13px;color:#adc7db;}
</style></head><body><div id="lab"><h1>Radial and conic UI</h1><p>Circles, ellipses, rings and angular color.</p><div id="grid">
<div class="card"><div class="label">Circle</div><div id="circle" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Ellipse / offset</div><div id="ellipse" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Repeating rings</div><div id="rings" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Conic colors</div><div id="conic" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Alpha wedges</div><div id="wedges" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Live / {{angle}}</div><div class="sample" data-style-decorator="'conic-gradient(from ' + angle + 'deg,#e96644,#4e96df)'"><div class="word">UI</div></div></div>
</div><p class="hint">UI Values / angle: 0..360. Tick: {{tick}}</p><div id="motion"/>
<button id="pause" data-event-click="command('Pause')">Pause</button><button id="resume" data-event-click="command('Resume')">Resume</button><button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml"
                                           : gradient_gallery ? R"rml(<rml><head><style>
body,div,p,h1 {display:block;} body {margin:0;width:100%;height:100%;font-family:Lato;font-size:15px;color:#e5f2ff;pointer-events:none;}
#lab {position:absolute;left:4%;top:2%;width:92%;}
h1 {font-size:23px;margin:0 0 3px;color:#79e7cf;} p {margin:3px 0;}
#grid {margin-top:6px;} .card {display:inline-block;width:30%;margin:0 2% 6px 0;vertical-align:top;}
.label {height:19px;font-size:13px;} .sample {position:relative;height:56px;background-color:#1b3347;border-radius:6px;}
.word {position:absolute;left:18%;top:20px;font-size:14px;color:white;}
#horizontal {decorator:horizontal-gradient(#e96644 #4e96df);}
#vertical {decorator:vertical-gradient(#63cf89 #4e96df);}
#stops {decorator:linear-gradient(90deg,#e96644 0%,#79e7cf 45%,#4e96df 100%);}
#repeat {decorator:repeating-linear-gradient(90deg,#e96644 0%,#4e96df 25%);}
#light,#dark {position:absolute;top:0;width:50%;height:56px;}
#light {left:0;background-color:#82a4c3;} #dark {left:50%;background-color:#173c50;}
#alpha {position:absolute;left:0;top:0;width:100%;height:56px;decorator:linear-gradient(90deg,#ff400000,#ff4000ff);}
#motion {width:100px;height:12px;margin-top:4px;animation:2s linear-in infinite alternate anglepulse;}
@keyframes anglepulse {from {decorator:linear-gradient(0deg,#e96644,#4e96df);} to {decorator:linear-gradient(180deg,#e96644,#4e96df);}}
button {display:inline-block;padding:5px 8px;margin:4px 5px 0 0;background-color:#30536e;pointer-events:auto;}
button:hover,button:focus {background-color:#4c849e;} .hint {font-size:13px;color:#adc7db;}
</style></head><body><div id="lab"><h1>UI gradients</h1><p>Angle, colors, alpha and repeating stops.</p><div id="grid">
<div class="card"><div class="label">Horizontal</div><div id="horizontal" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Vertical</div><div id="vertical" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Stops</div><div id="stops" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Alpha</div><div class="sample"><div id="light"/><div id="dark"/><div id="alpha"/><div class="word">UI</div></div></div>
<div class="card"><div class="label">Repeat</div><div id="repeat" class="sample"><div class="word">UI</div></div></div>
<div class="card"><div class="label">Live / {{angle}}</div><div class="sample" data-style-decorator="'linear-gradient(' + angle + 'deg,#e96644,#4e96df)'"><div class="word">UI</div></div></div>
</div><p class="hint">UI Values / angle: 0..360. Tick: {{tick}}</p><div id="motion"/>
<button id="pause" data-event-click="command('Pause')">Pause</button><button id="resume" data-event-click="command('Resume')">Resume</button><button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml"
                                           : backdrops        ? R"rml(<rml><head><style>
body,div,p,h1 {display:block;} body {margin:0;width:100%;height:100%;font-family:Lato;font-size:15px;color:#e5f2ff;pointer-events:none;}
#lab {position:absolute;left:4%;top:2%;width:92%;}
h1 {font-size:23px;margin:0 0 3px;color:#79e7cf;} p {margin:3px 0;}
#grid {margin-top:6px;} .card {display:inline-block;width:30%;margin:0 2% 6px 0;vertical-align:top;}
.label {height:19px;font-size:13px;} .sample {position:relative;height:56px;background-color:#1b3347;}
.red,.green,.blue {position:absolute;top:0;width:34%;height:56px;}
.red {left:0;background-color:#e96644;} .green {left:33%;background-color:#63cf89;} .blue {left:66%;background-color:#4e96df;}
.glass {position:absolute;left:12%;top:9px;width:76%;height:38px;background-color:#ffffff10;border-radius:6px;}
.word {position:absolute;left:20%;top:10px;font-size:14px;color:white;}
#blur {backdrop-filter:blur(3px);} #gray {backdrop-filter:grayscale(1);}
#contrast {backdrop-filter:brightness(0.6) contrast(0.9);} #front {filter:blur(3px);}
#pulse {position:relative;width:100px;height:12px;margin-top:4px;}
#pulseleft {position:absolute;left:0;top:0;width:50px;height:12px;background-color:#e96644;}
#pulseright {position:absolute;left:50px;top:0;width:50px;height:12px;background-color:#4e96df;}
#motion {position:absolute;left:0;top:0;width:100px;height:12px;animation:2s linear-in infinite alternate softness;}
@keyframes softness {from {backdrop-filter:blur(0px);} to {backdrop-filter:blur(4px);}}
button {display:inline-block;padding:5px 8px;margin:4px 5px 0 0;background-color:#30536e;pointer-events:auto;}
button:hover,button:focus {background-color:#4c849e;} .hint {font-size:13px;color:#adc7db;}
</style></head><body><div id="lab"><h1>UI backdrops</h1><p>Filter the background. Keep foreground text sharp.</p><div id="grid">
<div class="card"><div class="label">Reference</div><div class="sample"><div class="red"/><div class="green"/><div class="blue"/><div class="glass"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Blur</div><div class="sample"><div class="red"/><div class="green"/><div class="blue"/><div id="blur" class="glass"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Grayscale</div><div class="sample"><div class="red"/><div class="green"/><div class="blue"/><div id="gray" class="glass"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Contrast</div><div class="sample"><div class="red"/><div class="green"/><div class="blue"/><div id="contrast" class="glass"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Foreground</div><div class="sample"><div class="red"/><div class="green"/><div class="blue"/><div id="front" class="glass"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Live / {{blur}} px</div><div class="sample"><div class="red"/><div class="green"/><div class="blue"/><div class="glass" data-style-backdrop-filter="'blur(' + blur + 'px)'"><div class="word">UI</div></div></div></div>
</div><p class="hint">UI Values / blur: 0..16. Tick: {{tick}}</p><div id="pulse"><div id="pulseleft"/><div id="pulseright"/><div id="motion"/></div>
<button id="pause" data-event-click="command('Pause')">Pause</button><button id="resume" data-event-click="command('Resume')">Resume</button><button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml"
                                           : shadows          ? R"rml(<rml><head><style>
body,div,p,h1 {display:block;} body {margin:0;width:100%;height:100%;font-family:Lato;font-size:15px;color:#e5f2ff;pointer-events:none;}
#lab {position:absolute;left:4%;top:2%;width:92%;}
h1 {font-size:23px;margin:0 0 3px;color:#79e7cf;} p {margin:3px 0;}
#grid {margin-top:6px;} .card {display:inline-block;width:30%;margin:0 2% 6px 0;vertical-align:top;}
.label {height:19px;font-size:13px;} .sample {position:relative;height:56px;background-color:#1b3347;}
.shape {position:absolute;left:24%;top:14px;width:48%;height:24px;background-color:#e96644;}
.word {position:absolute;left:12%;top:4px;font-size:14px;color:white;}
#hard {filter:drop-shadow(#000000b0 10px 8px 0px);}
#left {filter:drop-shadow(#3388cc80 -8px 5px 0px);}
#soft {filter:drop-shadow(#000000b0 8px 6px 3px);}
#chain {filter:drop-shadow(#e9764480 -6px -4px 1px) drop-shadow(#4da9e080 8px 6px 2px);}
#motion {width:42px;height:8px;margin-top:4px;background-color:#79e7cf;animation:2s linear-in infinite alternate softness;}
@keyframes softness {from {filter:drop-shadow(#79e7cf80 -4px 3px 1px);} to {filter:drop-shadow(#79e7cf80 8px 3px 5px);}}
button {display:inline-block;padding:5px 8px;margin:4px 5px 0 0;background-color:#30536e;pointer-events:auto;}
button:hover,button:focus {background-color:#4c849e;} .hint {font-size:13px;color:#adc7db;}
</style></head><body><div id="lab"><h1>UI shadows</h1><p>Same shapes. Alpha shadows.</p><div id="grid">
<div class="card"><div class="label">Reference</div><div class="sample"><div class="shape"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Hard</div><div class="sample"><div id="hard" class="shape"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Left</div><div class="sample"><div id="left" class="shape"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Soft</div><div class="sample"><div id="soft" class="shape"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Stacked</div><div class="sample"><div id="chain" class="shape"><div class="word">UI</div></div></div></div>
<div class="card"><div class="label">Live / {{blur}} px</div><div class="sample"><div class="shape" data-style-filter="'drop-shadow(#000000b0 8px 6px ' + blur + 'px)'"><div class="word">UI</div></div></div></div>
</div><p class="hint">UI Values / blur: 0..16. Tick: {{tick}}</p><div id="motion"/>
<button id="pause" data-event-click="command('Pause')">Pause</button><button id="resume" data-event-click="command('Resume')">Resume</button><button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml"
                                                              : R"rml(<rml><head><style>
body,div,p,h1 {display:block;} body {margin:0;width:100%;height:100%;font-family:Lato;font-size:15px;color:#e5f2ff;pointer-events:none;}
#lab {position:absolute;left:4%;top:3%;width:92%;}
h1 {font-size:25px;margin:0 0 5px;color:#79e7cf;} p {margin:4px 0;}
#grid {margin-top:10px;} .card {display:inline-block;width:30%;margin:0 2% 10px 0;vertical-align:top;}
.label {height:21px;font-size:13px;} .sample {position:relative;height:58px;background-color:#1b3347;}
.red,.green,.blue {position:absolute;top:10px;width:30%;height:42px;}
.red {left:4%;background-color:#e96644;} .green {left:35%;background-color:#63cf89;} .blue {left:66%;background-color:#4e96df;}
.word {position:absolute;left:9%;top:24px;font-size:16px;color:white;}
#gray {filter:grayscale(1);} #bright {filter:brightness(1.6);} #contrast {filter:contrast(1.8);} #fade {filter:opacity(0.35);}
#motion {width:42px;height:12px;margin-top:8px;background-color:#79e7cf;animation:2s linear-in infinite alternate softness;}
@keyframes softness {from {filter:blur(0px);} to {filter:blur(5px);}}
button {display:inline-block;padding:7px 10px;margin:5px 5px 0 0;background-color:#30536e;pointer-events:auto;}
button:hover,button:focus {background-color:#4c849e;} .hint {font-size:13px;color:#adc7db;}
</style></head><body><div id="lab"><h1>UI filter lab</h1><p>Same shapes. Different filters.</p><div id="grid">
<div class="card"><div class="label">Reference</div><div class="sample"><div class="red"/><div class="green"/><div class="blue"/><div class="word">FORGE</div></div></div>
<div class="card"><div class="label">Grayscale / 1</div><div id="gray" class="sample"><div class="red"/><div class="green"/><div class="blue"/><div class="word">FORGE</div></div></div>
<div class="card"><div class="label">Brightness / 1.6</div><div id="bright" class="sample"><div class="red"/><div class="green"/><div class="blue"/><div class="word">FORGE</div></div></div>
<div class="card"><div class="label">Contrast / 1.8</div><div id="contrast" class="sample"><div class="red"/><div class="green"/><div class="blue"/><div class="word">FORGE</div></div></div>
<div class="card"><div class="label">Opacity / 0.35</div><div id="fade" class="sample"><div class="red"/><div class="green"/><div class="blue"/><div class="word">FORGE</div></div></div>
<div class="card"><div class="label">Blur / {{blur}} px</div><div id="blur" class="sample" data-style-filter="'blur(' + blur + 'px)'"><div class="red"/><div class="green"/><div class="blue"/><div class="word">FORGE</div></div></div>
</div><p class="hint">Inspector: Filter lab HUD / UI Values / blur (0 to 16).</p><p class="hint">The soft pulse keeps animating during Pause. Tick: {{tick}}</p><div id="motion"/>
<button id="pause" data-event-click="command('Pause')">Pause</button><button id="resume" data-event-click="command('Resume')">Resume</button><button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml");
    const auto ui = register_ui_document(root, ui_path);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name) {
        const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                            .at("selected")
                            .get<std::string>();
        auto entity = scene.entity(id);
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    if (fresh) {
        create(masks_gallery      ? "Mask lab HUD"
               : gradient_gallery ? "Gradient lab HUD"
               : backdrops        ? "Backdrop lab HUD"
               : shadows          ? "Shadow lab HUD"
                                  : "Filter lab HUD")
            .set<UiDocument>({{ui.id}, true, true, 0})
            .set<UiValues>({{{parameter, gradient_gallery ? 45. : 4.}}});
        auto camera = create("Game Camera");
        Camera view;
        view.background_r = .025f;
        view.background_g = .045f;
        view.background_b = .07f;
        camera.set(view).set<LocalTranslation>({0, 0, -8});
    } else {
        scene.load(root / scene_path);
        if (masks_gallery) {
            const auto existing = read_scene(root);
            for (const auto& row : existing.at("entities")) {
                const auto found = row.at("components").find("forge.ui_values");
                if (found != row.at("components").end() &&
                    found->at("values") == Json::array({{{"name", "blur"}, {"value", 4.}}}))
                    scene.entity(row.at("id").get<std::string>()).set<UiValues>({{{"angle", 45.}}});
            }
        }
    }
    scene.save(root / scene_path);
    const auto lease = std::make_shared<ProjectLease>(root);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, inspector(renderer, root, font), scene.schema());
    if (masks_gallery) {
        const auto catalog = AssetCatalog::open_project(root);
        const auto image =
            std::find_if(catalog.records().begin(), catalog.records().end(), [](const auto& row) {
                return row.second.source == "Assets/UI/BitmapFormats/health.png";
            });
        check(image != catalog.records().end(), "Canonical PNG mask asset missing");
        std::vector<AssetDependency> declared;
        for (const auto& edge : catalog.records().at(ui.id).dependency_edges)
            if (declared_runtime_edge(edge))
                declared.push_back(edge);
        declared.push_back(
            {image->first, "texture", AssetDependencyKind::Runtime, "declared:ui-mask-image", {}});
        declare_runtime_dependencies(*lease, ui.id, std::move(declared), catalog.document());
    }
}
std::vector<Pixels> exercise(DiligentPresentation& presentation, IDeviceContext* context,
                             const std::filesystem::path& root, const Json& source,
                             const std::vector<std::byte>& font,
                             const std::filesystem::path& images) {
    std::filesystem::create_directories(images);
    GamePresentation visual(presentation, context, root, font);
    visual.dimensions(640, 360, 1);
    GameSessionConfig config;
    config.content_root = root;
    config.ui = true;
    config.preparation = [&](std::uint64_t ticket) { return visual.prepare(ticket); };
    GameSession session(std::move(config));
    const auto submit = [&] {
        presentation.end_frame(context);
        context->Flush();
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    const auto ticket = session.prepare(source);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        presentation.begin_frame();
        const auto state = session.poll_preparation(ticket);
        submit();
        if (state.ready)
            break;
        check(std::chrono::steady_clock::now() < deadline, "Filter Gallery preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(ticket, RuntimeClock::Clock::now(), false);
    const auto tick = session.status().at("clock").at("tick");
    double presentation_time = 0;
    const auto capture = [&](double time, unsigned width, unsigned height, const char* label) {
        visual.dimensions(width, height, 1);
        // Exercise native animation in ordinary presentation steps, within the
        // existing long-frame clamp. Fixed simulation remains paused throughout.
        while (presentation_time + .000001 < time) {
            presentation_time = std::min(time, presentation_time + .05);
            presentation.begin_frame();
            check(visual.draw(session, presentation_time), "Filter animation display unavailable");
            submit();
        }
        Pixels pixels;
        for (unsigned frame = 0; frame < 3; ++frame) {
            presentation.begin_frame();
            auto* target = visual.draw(session, time);
            check(target, "Filter Gallery has no Game display");
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, target->GetTexture());
            submit();
        }
        check(session.status().at("clock").at("tick") == tick,
              "Presentation filters advanced fixed simulation while paused");
        check(visual.ui().diagnostic().empty(), "Filter Gallery reported a UI fault");
        check(Rml::GetNumContexts() == 1, "Filter Gallery must have one main UI context");
        auto* document = Rml::GetContext(0)->GetDocument(0);
        for (const char* id : {"pause", "resume", "step"}) {
            auto* control = document->GetElementById(id);
            check(control, "Filter Gallery control missing");
            const auto position = control->GetAbsoluteOffset();
            const auto size = control->GetBox().GetSize(Rml::BoxArea::Border);
            check(position.x >= 0 && position.y >= 0 && position.x + size.x <= width &&
                      position.y + size.y <= height,
                  ("Filter Gallery controls are outside " + std::to_string(width) + "x" +
                   std::to_string(height) + ": " + id + " at " + std::to_string(position.x) + "," +
                   std::to_string(position.y) + " size " + std::to_string(size.x) + "," +
                   std::to_string(size.y))
                      .c_str());
        }
        test_gallery_capture::ppm(images / (std::string(label) + ".ppm"), pixels, width, height);
        return pixels;
    };
    std::vector<Pixels> result;
    result.push_back(capture(.1, 640, 360, "filters"));
    const auto revision = visual.ui().presentation_revision();
    result.push_back(capture(1.1, 640, 360, "presentation-animation"));
    const auto changed = [](const Pixels& a, const Pixels& b) {
        unsigned count = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            count += a[i] != b[i];
        return count;
    };
    check(changed(result[0], result[1]) > 25, "RCSS filter animation did not change paused pixels");
    const auto row = std::find_if(
        source.at("entities").begin(), source.at("entities").end(),
        [](const auto& entity) { return entity.at("components").contains("forge.ui_values"); });
    check(row != source.at("entities").end(), "Filter Gallery value controller missing");
    session.active().tune_property(
        {source.at("asset_id").get<AssetId>(), row->at("id").get<EntityId>()}, "forge.ui_values",
        "values", Json::array({{{"name", parameter}, {"value", 0.}}}));
    result.push_back(
        capture(1.1, 640, 360, gradient_gallery ? "live-angle-zero" : "live-blur-zero"));
    check(changed(result[1], result[2]) > 200, "Inspector model did not alter actual blur pixels");
    check(visual.ui().presentation_revision() == revision,
          "Live filter value replaced UI contexts");
    result.push_back(capture(1.1, 360, 640, "portrait"));
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || (argc == 5 && (std::string_view(argv[4]) == "--shadows" ||
                                          (std::string_view(argv[4]) == "--backdrops" ||
                                           (std::string_view(argv[4]) == "--gradients" ||
                                            (std::string_view(argv[4]) == "--curved" ||
                                             std::string_view(argv[4]) == "--masks"))))),
              "Need Gallery, font and scratch; or --author Gallery font; optional "
              "--shadows/--backdrops/--gradients/--curved/--masks");
        shadows = argc == 5 && std::string_view(argv[4]) == "--shadows";
        backdrops = argc == 5 && std::string_view(argv[4]) == "--backdrops";
        masks_gallery = argc == 5 && std::string_view(argv[4]) == "--masks";
        curved_gallery = argc == 5 && std::string_view(argv[4]) == "--curved";
        gradient_gallery =
            curved_gallery || (argc == 5 && std::string_view(argv[4]) == "--gradients");
        if (gradient_gallery) {
            scene_path = "Scenes/164-ui-linear-gradients.scene.json";
            ui_path = "Assets/UI/Gradients/Lab.rml";
            parameter = "angle";
        }
        if (curved_gallery) {
            scene_path = "Scenes/165-ui-radial-conic-gradients.scene.json";
            ui_path = "Assets/UI/CurvedGradients/Lab.rml";
        }
        if (masks_gallery) {
            gradient_gallery = true;
            parameter = "angle";
            scene_path = "Scenes/166-ui-alpha-masks.scene.json";
            ui_path = "Assets/UI/Masks/Lab.rml";
        }
        if (backdrops) {
            scene_path = "Scenes/163-ui-backdrop-filters.scene.json";
            ui_path = "Assets/UI/Backdrops/Lab.rml";
        }
        if (shadows) {
            scene_path = "Scenes/162-ui-drop-shadows.scene.json";
            ui_path = "Assets/UI/Shadows/Lab.rml";
        }
        const bool editing = std::string_view(argv[1]) == "--author";
        const auto root = std::filesystem::absolute(editing ? argv[2] : argv[3]);
        const auto font = asset_detail::read_bytes(editing ? argv[3] : argv[2], 4 * 1024 * 1024);
        EngineVkCreateInfo create;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Filter Gallery Vulkan unavailable");
        UiDiligentRenderer renderer(device);
        if (editing) {
            author(root, renderer, font);
            return 0;
        }
        std::filesystem::remove_all(root);
        const auto project = root / "Project";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        const auto source = read_scene(project);
        DiligentPresentation presentation(device);
        const auto original =
            exercise(presentation, context, project, source, font, root / "source");
        const auto lease = std::make_shared<ProjectLease>(project);
        EngineContext engine;
        const std::array roots{source.at("asset_id").get<AssetId>()};
        Scene schema_scene(engine.world());
        const auto schema = schema_scene.schema();
        prepare_runtime_content_catalog(*lease, roots, inspector(renderer, project, font), schema);
        const RuntimePackageTarget target{"linux", "none"};
        std::filesystem::create_directories(root / "Relocated");
        package_runtime_content(project, root / "Relocated/content", roots, target, {}, {}, schema,
                                inspector(renderer, project, font));
        (void)open_runtime_content(root / "Relocated/content", target);
        std::filesystem::rename(project, root / "SourcesOffline");
        const auto exported = exercise(presentation, context, root / "Relocated/content", source,
                                       font, root / "export");
        check(original == exported, "Relocated filter Gallery pixels differ from authored source");
        std::cout << "Filter Gallery native Game pixels, paused model/animation, resize and "
                     "source-hidden export passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
