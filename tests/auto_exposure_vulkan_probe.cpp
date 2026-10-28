#include "Graphics/GraphicsEngine/interface/BufferView.h"
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "auto_exposure.hpp"
#include "frame_renderer.hpp"
#include "render_backend.hpp"
#include "sprite_draw_render_tests.hpp"
#include <cstring>
#include <iostream>

using namespace Diligent;
using Pixels = forge::test_sprite_draw::Pixels;
static void require(bool value, const char* message) {
    forge::test_sprite_draw::check(value, message);
}
static Pixels readback(IRenderDevice* device, IDeviceContext* context, ITextureView* view) {
    return forge::test_sprite_draw::read_pixels(device, context, view->GetTexture());
}
static void save(const Pixels& pixels, unsigned width, unsigned height,
                 const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto pixel : pixels)
        file.write(reinterpret_cast<const char*>(pixel.data()), 3);
    require(bool(file), "Exposure capture failed");
}
#include "auto_exposure_gallery_render_tests.hpp"
#include "display_resolve_tests.hpp"
#include "distance_fog_gallery_render_tests.hpp"

namespace {
template <class F> void rejected(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid automatic exposure candidate was admitted");
}
std::array<float, 4> state(IRenderDevice* device, IDeviceContext* context, IBufferView* view) {
    require(view != nullptr, "Missing exposure state view");
    BufferDesc desc;
    desc.Name = "FORGE exposure test-only synchronous state readback";
    desc.Size = 16;
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> copy;
    device->CreateBuffer(desc, nullptr, &copy);
    require(bool(copy), "Exposure state staging allocation failed");
    context->CopyBuffer(view->GetBuffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, copy, 0,
                        16, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    void* data = nullptr;
    context->MapBuffer(copy, MAP_READ, MAP_FLAG_NONE, data);
    require(data != nullptr, "Exposure state readback map failed");
    std::array<float, 4> result;
    std::memcpy(result.data(), data, sizeof(result));
    context->UnmapBuffer(copy, MAP_READ);
    return result;
}
void metering(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    using namespace forge;
    auto* device = presentation.device();
    TextureDesc desc;
    desc.Name = "FORGE partial-tile HDR exposure fixture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 17;
    desc.Height = 9;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> hdr;
    device->CreateTexture(desc, nullptr, &hdr);
    require(bool(hdr), "Exposure source allocation failed");
    AutoExposure meter(presentation);
    SceneAutoExposure settings;
    settings.enabled = true;
    settings.low_percentile = 0;
    settings.high_percentile = 1;
    settings.min_ev = -20;
    settings.max_ev = 20;
    settings.speed_up = 2;
    settings.speed_down = 4;
    const auto sample = [&](float gray, float delta, bool reset) {
        presentation.begin_frame();
        auto* rtv = hdr->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float value[]{gray, gray, gray, 1};
        context->ClearRenderTarget(rtv, value, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        auto* exposure = meter.apply(context, hdr->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                     settings, delta, reset);
        presentation.end_frame(context);
        auto result = state(device, context, exposure);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return result;
    };
    const auto first = sample(1.1f, 0, true);
    require(first[1] == 1 && first[3] == 153 &&
                std::abs(first[0] - (std::log2(.18) - std::log2(1.1))) < .13 &&
                first[0] == first[2],
            "Partial-tile histogram missed samples or initial gain exceeds its logarithmic "
            "quantization");
    const auto rising = sample(.01f, .1f, false);
    const double rise = rising[2] + (first[0] - rising[2]) * std::exp(-.2);
    require(rising[3] == 153 && std::abs(rising[2] - std::log2(.18 / .01)) < .13 &&
                std::abs(rising[0] - rise) < 1e-5 && rising[0] > first[0],
            "Rising EV did not follow the independent exponential time equation");
    const auto falling = sample(64, .1f, false);
    const double fall = falling[2] + (rising[0] - falling[2]) * std::exp(-.4);
    require(std::abs(falling[2] - std::log2(.18 / 64)) < .13 &&
                std::abs(falling[0] - fall) < 1e-5 && falling[0] < rising[0],
            "Falling EV used the wrong rate or stale histogram data");
    const auto stationary = sample(.01f, 0, false);
    require(std::abs(stationary[0] - falling[0]) < 1e-5,
            "Exposure advanced despite zero elapsed presentation time");
    settings.min_ev = -2;
    settings.max_ev = 2;
    require(sample(.00001f, 0, true)[0] == 2 && sample(1024, 0, true)[0] == -2,
            "Exposure did not enforce both authored gain limits");
    const auto black = sample(0, .25f, false);
    require(black[0] == 0 && black[1] == 0 && black[3] == 0,
            "Empty black pixels invented metering information or poisoned history");
    const auto invalid = sample(std::numeric_limits<float>::quiet_NaN(), .25f, false);
    require(invalid[0] == 0 && invalid[1] == 0 && invalid[3] == 0,
            "Nonfinite HDR pixels entered exposure statistics");
    settings.min_ev = -20;
    settings.max_ev = 20;
    const auto returned = sample(.01f, 0, false);
    require(returned[0] == returned[2], "First valid image retained empty/invalid visual history");
    const auto misses = presentation.cache_misses();
    AutoExposure disabled(presentation);
    require(disabled.apply(context, hdr->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), {}, 0,
                           false) == nullptr &&
                presentation.cache_misses() == misses,
            "Disabled metering compiled or allocated the automatic path");
    meter.clear_source();
}
void percentile_metering(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    using namespace forge;
    // Ten immutable RGBA32 pixels: one dark outlier, seven dark subjects and
    // two highlights. Percentile clipping must retain partial histogram bins.
    std::array<std::array<float, 4>, 10> values;
    for (unsigned i = 0; i < values.size(); ++i) {
        const float gray = i == 0 ? .001f : i < 8 ? .02f : .9f;
        values[i] = {gray, gray, gray, 1};
    }
    TextureDesc desc;
    desc.Name = "FORGE exposure independent percentile fixture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = unsigned(values.size());
    desc.Height = 1;
    desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    desc.Usage = USAGE_IMMUTABLE;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    TextureSubResData sub{values.data(), sizeof(values), 0};
    Diligent::TextureData data{&sub, 1};
    RefCntAutoPtr<ITexture> source;
    presentation.device()->CreateTexture(desc, &data, &source);
    require(bool(source), "Percentile source allocation failed");
    AutoExposure meter(presentation);
    SceneAutoExposure settings;
    settings.enabled = true;
    settings.min_ev = -20;
    settings.max_ev = 20;
    const auto sample = [&](float low, float high) {
        settings.low_percentile = low;
        settings.high_percentile = high;
        presentation.begin_frame();
        auto* output = meter.apply(context, source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                   settings, 0, true);
        presentation.end_frame(context);
        const auto result = state(presentation.device(), context, output);
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        return result;
    };
    const auto middle = sample(.1f, .8f);
    require(middle[3] == 10 && std::abs(middle[0] - (std::log2(.18) + 5.625)) < 1e-5,
            "Whole-bin percentile clipping retained an outlier");
    const auto partial = sample(.15f, .85f);
    const double mean = (6.5 * -5.625 + .5 * -.125) / 7;
    require(std::abs(partial[0] - (std::log2(.18) - mean)) < 1e-5,
            "Percentile boundaries did not weight partial histogram bins");
    // A rejected candidate must preserve the previously derived scalar history.
    const auto retained = partial;
    settings.low_percentile = settings.high_percentile;
    rejected([&] {
        meter.apply(context, source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), settings, 0,
                    false);
    });
    settings.low_percentile = .15f;
    presentation.begin_frame();
    auto* output = meter.apply(context, source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                               settings, 0, false);
    presentation.end_frame(context);
    require(state(presentation.device(), context, output) == retained,
            "Rejected exposure settings replaced last-good history");
    context->FinishFrame();
}
void composed_frames(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    using namespace forge;
    FrameRenderer automatic(presentation), manual(presentation);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.auto_exposure.enabled = true;
    auto& settings = scene.settings.auto_exposure;
    settings.min_ev = -20;
    settings.max_ev = 20;
    settings.low_percentile = 0;
    settings.high_percentile = 1;
    RenderCamera camera;
    camera.entity = EntityId::generate();
    camera.camera.background_r = camera.camera.background_g = camera.camera.background_b = .02f;
    scene.cameras = {camera};
    const auto render = [&](FrameRenderer& frame, const RenderScene& source, double seconds,
                            unsigned width = 64) {
        presentation.begin_frame();
        auto* output = frame.game(context, source, width, 32, seconds);
        presentation.end_frame(context);
        auto pixels = readback(presentation.device(), context, output);
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        require(frame.diagnostics().empty(),
                "Composed automatic exposure reported a rendering error");
        return pixels;
    };
    const auto initial = render(automatic, scene, 0);
    scene.cameras[0].camera.background_r = scene.cameras[0].camera.background_g =
        scene.cameras[0].camera.background_b = .9f;
    const auto transition = render(automatic, scene, .1);
    const auto progressed = render(automatic, scene, .2);
    require(transition[0][0] > initial[0][0] + 30 && progressed[0][0] < transition[0][0],
            "Game exposure failed to adapt from the host's unscaled timestamp");
    const auto rewind = render(automatic, scene, .05);
    require(std::abs(int(rewind[0][0]) - int(initial[0][0])) <= 8,
            "Clock rewind did not reset visual exposure history");
    scene.cameras[0].camera.background_r = scene.cameras[0].camera.background_g =
        scene.cameras[0].camera.background_b = .02f;
    const auto changed = render(automatic, scene, .15);
    require(changed[0][0] < initial[0][0] - 20, "Dark transition snapped instead of adapting");
    scene.scene = AssetId::generate();
    const auto new_scene = render(automatic, scene, .25);
    require(std::abs(int(new_scene[0][0]) - int(initial[0][0])) <= 1,
            "A new Scene retained exposure from another Scene");
    scene.cameras[0].camera.viewport_width = .5;
    auto second = scene.cameras[0];
    second.entity = EntityId::generate();
    second.camera.viewport_x = .5;
    second.camera.background_r = second.camera.background_g = second.camera.background_b = .9f;
    scene.cameras.push_back(second);
    const auto composed = render(automatic, scene, .35);
    auto reference = scene;
    reference.settings.auto_exposure.enabled = false;
    // Independent equal-area two-band log mean. Both values lie safely inside
    // their known quarter-stop bins, not on a floating-point bin boundary.
    reference.settings.exposure = float(std::log2(.18) - (-5.625 - .125) / 2);
    const auto expected = render(manual, reference, .35);
    require(composed.size() == expected.size(), "Composed exposure changed viewport extent");
    for (std::size_t i = 0; i < composed.size(); ++i)
        for (unsigned lane = 0; lane < 4; ++lane)
            require(std::abs(int(composed[i][lane]) - int(expected[i][lane])) <= 2,
                    "Exposure was metered per camera instead of once on the composed HDR image");
    scene.settings.auto_exposure.enabled = false;
    const auto off = render(automatic, scene, .45);
    scene.settings.auto_exposure.enabled = true;
    const auto back = render(automatic, scene, .55);
    require(back == composed && off != back, "Re-enabling exposure reused stale visual history");

    // Every existing view/resource identity boundary resets derived history.
    // Establish bright history, then simultaneously darken and change one key;
    // a fresh renderer is an independent initial-frame reference.
    auto single = scene;
    single.cameras.resize(1);
    single.cameras[0].camera.viewport_width = 1;
    for (unsigned boundary = 0; boundary < 5; ++boundary) {
        FrameRenderer prior(presentation), fresh(presentation);
        for (auto* lane :
             {&single.cameras[0].camera.background_r, &single.cameras[0].camera.background_g,
              &single.cameras[0].camera.background_b})
            *lane = .9f;
        render(prior, single, 10);
        auto next = single;
        next.cameras[0].camera.background_r = next.cameras[0].camera.background_g =
            next.cameras[0].camera.background_b = .02f;
        unsigned width = 64;
        if (boundary == 0)
            width = 73;
        else if (boundary == 1)
            next.cameras[0].entity = EntityId::generate();
        else if (boundary == 2)
            next.cameras[0].camera.viewport_width = .75;
        else if (boundary == 3)
            next.cameras[0].camera.projection = 1;
        else
            prior.resources(nullptr);
        require(render(prior, next, 10.1, width) == render(fresh, next, 10.1, width),
                "Target/camera/viewport/projection/resources retained unrelated exposure history");
    }
    // Manual EV compensation and grading follow metering, and cannot feed it.
    scene.settings.exposure = .75f;
    scene.settings.color_adjustments = {true, {.8f, 1, .6f}, .65f, 1.2f};
    reference = scene;
    reference.settings.auto_exposure.enabled = false;
    reference.settings.exposure += float(std::log2(.18) + 2.875);
    const auto graded = render(automatic, scene, .65);
    const auto graded_reference = render(manual, reference, .65);
    for (std::size_t i = 0; i < graded.size(); ++i)
        for (unsigned lane = 0; lane < 4; ++lane)
            require(std::abs(int(graded[i][lane]) - int(graded_reference[i][lane])) <= 2,
                    "Manual compensation or grading fed back into automatic metering");
    const auto* retained_view = automatic.output();
    auto invalid = scene;
    invalid.settings.auto_exposure.max_ev = -21;
    rejected([&] { automatic.game(context, invalid, 97, 43, .75); });
    rejected(
        [&] { automatic.game(context, scene, 97, 43, std::numeric_limits<double>::quiet_NaN()); });
    invalid = scene;
    invalid.settings.distance_fog.end = invalid.settings.distance_fog.start;
    rejected([&] { automatic.game(context, invalid, 97, 43, .75); });
    invalid = scene;
    invalid.settings.distance_fog.color[0] = std::numeric_limits<float>::quiet_NaN();
    rejected([&] { automatic.game(context, invalid, 97, 43, .75); });
    require(automatic.output() == retained_view &&
                readback(presentation.device(), context, automatic.output()) == graded,
            "Rejected settings/time replaced the last-good composed display image");
    require(render(automatic, scene, .75) == graded,
            "Rejected frame advanced or damaged the accepted exposure state");
}
void disabled_compute_admission() {
    // Vulkan exposes compute as a core capability even when the request says
    // disabled. Exercise the same capability admission owner with explicit
    // disabled device metadata; do not claim a compute-less GPU was created.
    Diligent::RenderDeviceInfo info;
    info.Features.ComputeShaders = DEVICE_FEATURE_STATE_DISABLED;
    ShaderCreateInfo shader;
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    rejected([&] { forge::prepare_renderer_shader(info, shader); });
}
void inactive_sources(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    forge::DisplayResolve display(presentation);
    RefCntWeakPtr<ITexture> previous;
    for (unsigned step = 0; step < 4; ++step) {
        TextureDesc desc;
        desc.Name = "FORGE inactive display-source lifetime fixture";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = 17 + step;
        desc.Height = 9;
        desc.Format = TEX_FORMAT_RGBA16_FLOAT;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> source;
        presentation.device()->CreateTexture(desc, nullptr, &source);
        require(bool(source), "Display lifetime source allocation failed");
        presentation.begin_frame();
        auto* rtv = source->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float value[]{.3f, .3f, .3f, 1};
        context->ClearRenderTarget(rtv, value, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        forge::SceneAutoExposure settings;
        settings.enabled = step % 2 == 0;
        display.resolve(context, source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0, {},
                        settings, 0, true);
        presentation.end_frame(context);
        context->WaitForIdle();
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        require(!previous.Lock(), "An inactive display/meter binding retained the old HDR target");
        previous = RefCntWeakPtr<ITexture>(source);
    }
}

} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected exposure image directory and Gallery project/package");
        const std::filesystem::path images(argv[1]);
        std::filesystem::create_directories(images);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        info.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        require(device && context, "Exposure Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        check_display_resolve(presentation, context, images);
        metering(presentation, context);
        percentile_metering(presentation, context);
        composed_frames(presentation, context);
        disabled_compute_admission();
        inactive_sources(presentation, context);
        forge::test_exposure_gallery::exercise(presentation, context, argv[2], images);
        forge::test_fog_gallery::exercise(
            presentation, context,
            [&](auto* texture) {
                return forge::test_sprite_draw::read_pixels(device, context, texture);
            },
            argv[2], images);
        std::cout << "Automatic exposure metering/adaptation/composition and manual display pixels "
                     "passed: "
                  << device->GetAdapterInfo().Description << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
