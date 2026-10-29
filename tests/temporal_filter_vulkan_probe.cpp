#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "display_resolve.hpp"
#include "mesh_draw_shader.hpp"
#include "render_backend.hpp"
#include "sprite_draw_render_tests.hpp"
#include "temporal_filter.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
using namespace Diligent;
using Row = std::array<float, 4>;
using Clips = std::array<Row, 3>;
static void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> static void rejected(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    check(failed, "Invalid temporal input was admitted");
}
static RefCntAutoPtr<ITexture> texture(IRenderDevice* device, unsigned w, unsigned h,
                                       TEXTURE_FORMAT format, BIND_FLAGS flags) {
    TextureDesc desc;
    desc.Name = "FORGE original temporal preflight";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = w;
    desc.Height = h;
    desc.Format = format;
    desc.BindFlags = flags;
    RefCntAutoPtr<ITexture> result;
    device->CreateTexture(desc, nullptr, &result);
    check(bool(result), "Texture allocation failed");
    return result;
}
static RefCntAutoPtr<IPipelineState> pipeline(forge::DiligentPresentation& presentation,
                                              const char* vs, const char* ps,
                                              std::span<const TEXTURE_FORMAT> formats,
                                              bool geometry) {
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE original temporal preflight shader";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = vs;
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation.shader(shader, &vertex);
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = ps;
    presentation.shader(shader, &pixel);
    check(vertex && pixel, "Shader compilation failed");
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "FORGE original temporal preflight pass";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    auto& g = ci.GraphicsPipeline;
    g.NumRenderTargets = Uint8(formats.size());
    for (unsigned i = 0; i < formats.size(); ++i)
        g.RTVFormats[i] = formats[i];
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.DepthStencilDesc.DepthEnable = geometry;
    g.DepthStencilDesc.DepthWriteEnable = geometry;
    g.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;
    g.DSVFormat = geometry ? TEX_FORMAT_D32_FLOAT : TEX_FORMAT_UNKNOWN;
    ci.pVS = vertex;
    ci.pPS = pixel;
    RefCntAutoPtr<IPipelineState> result;
    presentation.graphics(ci, &result);
    check(bool(result), "Pipeline creation failed");
    return result;
}
static RefCntAutoPtr<IBuffer> buffer(IRenderDevice* device, unsigned bytes) {
    BufferDesc d;
    d.Name = "FORGE temporal test uniforms";
    d.Size = bytes;
    d.Usage = USAGE_DEFAULT;
    d.BindFlags = BIND_UNIFORM_BUFFER;
    RefCntAutoPtr<IBuffer> result;
    device->CreateBuffer(d, nullptr, &result);
    check(bool(result), "Uniform allocation failed");
    return result;
}
static void bind(IShaderResourceBinding* srb, SHADER_TYPE stage, const char* name,
                 IDeviceObject* value) {
    auto* v = srb->GetVariableByName(stage, name);
    check(v != nullptr, "Missing shader binding");
    v->Set(value);
}
static Row read_motion(IRenderDevice* device, IDeviceContext* context, ITexture* image, unsigned x,
                       unsigned y) {
    return forge::test_sprite_draw::read_float4_pixel(device, context, image, x, y);
}
static std::array<unsigned, 2> check_motion_at(IRenderDevice* device, IDeviceContext* context,
                                               ITexture* motion, unsigned width, unsigned height,
                                               const Clips& raster, const Clips& oldRaster,
                                               std::array<float, 2> offset,
                                               std::array<float, 2> lastJitter, bool valid = true) {
    unsigned x = 0, y = 0;
    double px = 0, py = 0;
    for (auto c : raster) {
        px += c[0] / c[3] / 3.;
        py += c[1] / c[3] / 3.;
    }
    x = unsigned((px * .5 + .5) * width);
    y = unsigned((.5 - py * .5) * height);
    auto observed = read_motion(device, context, motion, x, y);
    if (!valid) {
        check(observed[3] == 0, "New geometry unexpectedly has completed-frame motion");
        return {x, y};
    }
    check(observed[3] == 1, "Expected triangle interior missing");
    const double sx = 2 * (x + .5) / width - 1, sy = 1 - 2 * (y + .5) / height;
    double vx[3], vy[3];
    for (unsigned k = 0; k < 3; ++k) {
        vx[k] = raster[k][0] / raster[k][3];
        vy[k] = raster[k][1] / raster[k][3];
    }
    const double det = (vy[1] - vy[2]) * (vx[0] - vx[2]) + (vx[2] - vx[1]) * (vy[0] - vy[2]);
    std::array<double, 3> weights{
        ((vy[1] - vy[2]) * (sx - vx[2]) + (vx[2] - vx[1]) * (sy - vy[2])) / det,
        ((vy[2] - vy[0]) * (sx - vx[2]) + (vx[0] - vx[2]) * (sy - vy[2])) / det, 0};
    weights[2] = 1 - weights[0] - weights[1];
    double prior[4]{};
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned axis = 0; axis < 4; ++axis)
            prior[axis] += weights[k] * oldRaster[k][axis] / raster[k][3];
    const Row expected{float(sx - offset[0] - (prior[0] / prior[3] - lastJitter[0])),
                       float(sy - offset[1] - (prior[1] / prior[3] - lastJitter[1])),
                       float(prior[2] / prior[3]), 1};
    for (unsigned axis = 0; axis < 3; ++axis)
        check(std::abs(expected[axis] - observed[axis]) < .00003,
              "Raster motion/previous depth disagrees with perspective interpolation oracle");
    return {x, y};
}
static Clips positions(unsigned frame, unsigned mode, bool perspective, unsigned w, unsigned h,
                       std::array<float, 4>* depthProjection = nullptr, unsigned clipCase = 0) {
    forge::Camera camera;
    camera.projection = perspective ? 0u : 1u;
    camera.near_plane = clipCase == 0 ? .1 : clipCase == 1 ? .01 : .001;
    camera.far_plane = clipCase == 0 ? 20 : clipCase == 1 ? 10000 : 1000000;
    camera.orthographic_height = 4;
    forge::AffineTransform world;
    world.m[3] = mode == 2 ? .018 * frame : 0;
    const auto view = forge::camera_view(camera, world, w, h);
    if (depthProjection)
        *depthProjection = {view.projection[10], view.projection[11], view.projection[14],
                            view.projection[15]};
    const double shift = mode == 1 ? .018 * frame : 0;
    const double bend = mode == 3 ? .14 * std::sin(.35 * frame) : 0;
    const std::array<std::array<double, 3>, 3> vertices{
        {{{-.9 + shift, -.8, 3}}, {{.9 + shift, -.5, 4}}, {{-.1 + shift + bend, .9, 3.5}}}};
    Clips result{};
    for (unsigned i = 0; i < 3; ++i) {
        double v[4] = {};
        for (unsigned axis = 0; axis < 3; ++axis) {
            const auto& basis = axis == 0 ? view.right : axis == 1 ? view.up : view.forward;
            for (unsigned c = 0; c < 3; ++c)
                v[axis] += basis[c] * (vertices[i][c] - view.position[c]);
        }
        v[3] = 1;
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned c = 0; c < 4; ++c)
                result[i][row] += float(view.projection[row * 4 + c] * v[c]);
    }
    return result;
}
static void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context,
                     bool perspective, unsigned mode, unsigned clipCase) {
    constexpr unsigned width = 96, height = 80;
    auto* device = presentation.device();
    auto color = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                         BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    auto motion = texture(device, width, height, TEX_FORMAT_RGBA32_FLOAT,
                          BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    auto depth = texture(device, width, height, TEX_FORMAT_D32_FLOAT,
                         BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE);
    const char* vertex = R"HLSL(
cbuffer Pose {float4 Current[3];float4 Previous[3];float4 Jitter;};
struct V {float4 position:SV_POSITION;float4 current:TEXCOORD0;float4 previous:TEXCOORD1;};
V main(uint vertex:SV_VertexID){V o;o.current=Current[vertex];o.previous=Previous[vertex];o.position=o.current;return o;}
)HLSL";
    const char* pixel = R"HLSL(
cbuffer Pose {float4 Current[3];float4 Previous[3];float4 Jitter;};
struct Out {float4 color:SV_Target0;float4 motion:SV_Target1;};
Out main(float4 position:SV_POSITION,float4 current:TEXCOORD0,float4 previous:TEXCOORD1){
 Out o;o.color=float4(.8,.55,.2,1);float2 now=current.xy/current.w-Jitter.xy;
 float2 before=previous.xy/previous.w-Jitter.zw;o.motion=float4(now-before,previous.z/previous.w,1);return o;}
)HLSL";
    const std::array<TEXTURE_FORMAT, 2> formats{TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_RGBA32_FLOAT};
    auto geometry = pipeline(presentation, vertex, pixel, formats, true);
    RefCntAutoPtr<IShaderResourceBinding> drawBinding;
    geometry->CreateShaderResourceBinding(&drawBinding, true);
    auto pose = buffer(device, 7 * sizeof(Row));
    bind(drawBinding, SHADER_TYPE_VERTEX, "Pose", pose);
    bind(drawBinding, SHADER_TYPE_PIXEL, "Pose", pose);
    forge::TemporalFilter filter(presentation);
    std::unique_ptr<forge::TemporalFilter> uninterrupted;
    if (mode == 0 && clipCase == 0)
        uninterrupted = std::make_unique<forge::TemporalFilter>(presentation);
    forge::DisplayResolve display(presentation);
    forge::test_sprite_draw::Pixels lastRaw, lastFiltered;
    double rawVariation = 0, filteredVariation = 0;
    std::array<float, 4> projection;
    Clips previous = positions(0, mode, perspective, width, height, &projection, clipCase);
    auto lastJitter = forge::TemporalFilter::jitter(0, width, height);
    for (unsigned frame = 0; frame < 12; ++frame) {
        presentation.begin_frame();
        auto current = positions(frame, mode, perspective, width, height, nullptr, clipCase);
        const auto offset = forge::TemporalFilter::jitter(frame, width, height);
        Clips raster = current, oldRaster = previous;
        for (unsigned i = 0; i < 3; ++i) {
            raster[i][0] += offset[0] * raster[i][3];
            raster[i][1] += offset[1] * raster[i][3];
            oldRaster[i][0] += lastJitter[0] * oldRaster[i][3];
            oldRaster[i][1] += lastJitter[1] * oldRaster[i][3];
        }
        std::array<Row, 7> values{};
        for (unsigned i = 0; i < 3; ++i) {
            values[i] = raster[i];
            values[i + 3] = oldRaster[i];
        }
        values[6] = {offset[0], offset[1], lastJitter[0], lastJitter[1]};
        context->UpdateBuffer(pose, 0, sizeof(values), values.data(),
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        std::array<ITextureView*, 2> targets{color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                             motion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
        context->SetRenderTargets(2, targets.data(), dsv,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float dark[]{.02f, .02f, .02f, 1}, none[]{0, 0, 1, 0};
        context->ClearRenderTarget(targets[0], dark, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(targets[1], none, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, float(width), float(height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        context->SetPipelineState(geometry);
        context->CommitShaderResources(drawBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawAttribs draw;
        draw.NumVertices = 3;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
        const auto point = check_motion_at(device, context, motion, width, height, raster,
                                           oldRaster, offset, lastJitter);
        const auto x = point[0], y = point[1];
        const bool reset = frame == 0 || frame == 9;
        forge::TemporalFilter::Input input;
        input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.motion = motion->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.previous_depth_projection = projection;
        input.jitter = offset;
        input.previous_jitter = lastJitter;
        input.reset = reset;
        if (uninterrupted && frame == 4) {
            const auto bad = [&](auto edit) {
                auto invalid = input;
                invalid.reset = true;
                edit(invalid);
                rejected([&] { filter.resolve(context, invalid); });
                check(filter.history_bytes() == width * height * 20,
                      "Rejected temporal input changed retained targets");
            };
            bad([](auto& value) { value.color = nullptr; });
            bad([&](auto& value) {
                value.color = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            });
            bad([&](auto& value) { value.motion = input.color; });
            bad([&](auto& value) { value.depth = input.color; });
            bad([](auto& value) { value.stability = -.01f; });
            bad([](auto& value) { value.stability = .96f; });
            bad([](auto& value) { value.stability = std::numeric_limits<float>::infinity(); });
            bad([](auto& value) { value.stability = std::numeric_limits<float>::quiet_NaN(); });
            bad([](auto& value) { value.previous_depth_projection = {}; });
            bad([](auto& value) {
                value.previous_depth_projection[0] = std::numeric_limits<float>::infinity();
            });
            bad([](auto& value) { value.jitter[0] = std::numeric_limits<float>::quiet_NaN(); });
            bad([](auto& value) {
                value.previous_jitter[1] = std::numeric_limits<float>::infinity();
            });
        }
        auto* filtered = filter.resolve(context, input);
        check(filter.history_bytes() == width * height * 20, "Temporal history budget differs");
        auto* view = display.resolve(context, filtered, 0);
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        const auto actual =
            forge::test_sprite_draw::read_pixels(device, context, view->GetTexture());
        if (uninterrupted) {
            auto* baseline = display.resolve(context, uninterrupted->resolve(context, input), 0);
            context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            check(actual ==
                      forge::test_sprite_draw::read_pixels(device, context, baseline->GetTexture()),
                  "Rejected input disturbed the uninterrupted temporal result");
        }
        check(actual[y * width + x][0] > 100, "Temporal output lost foreground");
        if (reset || mode == 0) {
            auto* source =
                display.resolve(context, color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0);
            context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            const auto reference =
                forge::test_sprite_draw::read_pixels(device, context, source->GetTexture());
            for (unsigned p = 0; p < actual.size(); ++p)
                for (unsigned axis = 0; axis < 4; ++axis)
                    if (reset)
                        check(std::abs(int(actual[p][axis]) - int(reference[p][axis])) <= 1,
                              "Reset did not retain exact current frame");
            if (mode == 0) {
                if (frame >= 2 && frame < 9)
                    for (unsigned p = 0; p < actual.size(); ++p)
                        for (unsigned axis = 0; axis < 3; ++axis) {
                            const double raw = int(reference[p][axis]) - lastRaw[p][axis];
                            const double filtered = int(actual[p][axis]) - lastFiltered[p][axis];
                            rawVariation += raw * raw;
                            filteredVariation += filtered * filtered;
                        }
                lastRaw = reference;
                lastFiltered = actual;
            }
        }
        previous = current;
        lastJitter = offset;
        presentation.end_frame(context);
        context->FinishFrame();
        device->ReleaseStaleResources();
    }
    if (mode == 0) {
        std::cout << "Stationary display-space squared variation: raw=" << rawVariation
                  << " filtered=" << filteredVariation << '\n';
        check(rawVariation > 0 && filteredVariation < .7 * rawVariation,
              "Temporal filter did not reduce stationary jitter variation");
    }
    std::cout << (perspective ? "perspective" : "orthographic") << " clip=" << clipCase
              << " mode=" << mode << " 12 real motion/depth/filter frames passed\n";
}
static void boundaries(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    using forge::TemporalFilter;
    rejected([] { TemporalFilter::jitter(0, 0, 1); });
    rejected([] { TemporalFilter::jitter(0, 1, 0); });
    std::set<std::array<float, 2>> samples;
    std::array<double, 2> sum{};
    for (unsigned frame = 0; frame < 8; ++frame) {
        const auto sample = TemporalFilter::jitter(frame, 1, 1);
        for (unsigned axis = 0; axis < 2; ++axis) {
            check(std::isfinite(sample[axis]) && std::abs(sample[axis]) < 1,
                  "Temporal jitter exceeds its subpixel footprint");
            sum[axis] += sample[axis];
        }
        samples.insert(sample);
    }
    check(samples.size() == 8 && sum == std::array<double, 2>{},
          "Temporal jitter is repeated or biased");
    check(TemporalFilter::jitter(UINT64_MAX, 13, 11) == TemporalFilter::jitter(7, 13, 11),
          "Temporal jitter overflowed its frame sequence");
    rejected([&] { TemporalFilter invalid(presentation, 19); });
    struct Fixture {
        RefCntAutoPtr<ITexture> color, motion, depth;
        TemporalFilter::Input input;
    };
    auto* device = presentation.device();
    const auto fixture = [&](unsigned width, unsigned height) {
        Fixture result;
        std::vector<std::array<std::uint16_t, 4>> pixels(width * height);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
                pixels[y * width + x] = {std::uint16_t((x + y) % 2 ? 0x3c00 : 0), 0, 0,
                                         std::uint16_t((x + y) % 2 ? 0x3400 : 0x3a00)};
        TextureDesc desc;
        desc.Name = "FORGE temporal resize checker fixture";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA16_FLOAT;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        TextureSubResData subresource;
        subresource.pData = pixels.data();
        subresource.Stride = width * sizeof(pixels[0]);
        TextureData data;
        data.pSubResources = &subresource;
        data.NumSubresources = 1;
        device->CreateTexture(desc, &data, &result.color);
        check(bool(result.color), "Temporal checker fixture allocation failed");
        result.motion = texture(device, width, height, TEX_FORMAT_RGBA32_FLOAT,
                                BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
        result.depth = texture(device, width, height, TEX_FORMAT_D32_FLOAT,
                               BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE);
        const float motion[]{0, 0, .5f, 1};
        context->ClearRenderTarget(result.motion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                   motion, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(result.depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                   CLEAR_DEPTH_FLAG, .5f, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        result.input.color = result.color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        result.input.motion = result.motion->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        result.input.depth = result.depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        result.input.previous_depth_projection = {1, 0, 0, 1};
        return result;
    };
    presentation.begin_frame();
    TemporalFilter filter(presentation, 23 * 19 * 20);
    forge::DisplayResolve display(presentation);
    const auto first = fixture(23, 19), second = fixture(13, 11), excessive = fixture(24, 20);
    const auto exact_current = [&](const Fixture& value) {
        auto* actual = display.resolve(context, filter.resolve(context, value.input), 0);
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        const auto pixels =
            forge::test_sprite_draw::read_pixels(device, context, actual->GetTexture());
        auto* expected = display.resolve(context, value.input.color, 0);
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        const auto reference =
            forge::test_sprite_draw::read_pixels(device, context, expected->GetTexture());
        static unsigned checker_call = 0;
        ++checker_call;
        if (pixels != reference) {
            for (std::size_t p = 0; p < pixels.size(); ++p)
                if (pixels[p] != reference[p]) {
                    std::cerr << "Checker call=" << checker_call << " pixel=" << p << " actual=";
                    for (auto v : pixels[p])
                        std::cerr << unsigned(v) << ',';
                    std::cerr << " expected=";
                    for (auto v : reference[p])
                        std::cerr << unsigned(v) << ',';
                    std::cerr << '\n';
                    break;
                }
            throw std::runtime_error(
                "First/resized/invalidated history did not start from the current checker");
        }
    };
    exact_current(first);
    exact_current(first); // Accepted history retains current alpha as well as unchanged RGB.
    const float invalid_motion[]{0, 0, .5f, 0};
    context->ClearRenderTarget(first.motion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                               invalid_motion, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    exact_current(first); // Background/missing correspondence retains current alpha.
    exact_current(second);
    check(filter.history_bytes() == 13 * 11 * 20, "Resize retained the wrong history estimate");
    rejected([&] { filter.resolve(context, excessive.input); });
    check(filter.history_bytes() == 13 * 11 * 20, "Failed growth discarded the admitted targets");
    filter.invalidate();
    exact_current(second);
    presentation.end_frame(context);
    context->WaitForIdle();
    context->FinishFrame();
    device->ReleaseStaleResources();
}
#include "temporal_mesh_motion_tests.hpp"
#include "temporal_scene_render_tests.hpp"
#include "temporal_view_boundary_tests.hpp"
int main(int argc, char** argv) {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        info.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        if (argc == 2 && std::string_view(argv[1]) == "--normal-geometry") {
            test_temporal_mesh_motion::exercise_normals(presentation, context);
            context->WaitForIdle();
            return 0;
        }
        check(argc == 3, "Expected isolated fixture and Gallery project directories");
        forge::test_temporal_scene::exercise(presentation, context, argv[1]);
        forge::test_temporal_scene::gallery(presentation, context, argv[2],
                                            std::filesystem::path(argv[1]) / "images");
        temporal_view_boundaries(presentation, context);
        boundaries(presentation, context);
        test_temporal_mesh_motion::exercise_all(presentation, context);
        for (bool perspective : {false, true})
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned clipCase = 0; clipCase < 3; ++clipCase)
                    exercise(presentation, context, perspective, mode, clipCase);
        context->WaitForIdle();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
