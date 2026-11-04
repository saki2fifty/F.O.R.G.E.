#pragma once
#include "distance_fog_shader.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

// Included after the temporal probe's shared pipeline and pixel-readback helpers.
namespace forge::test_fog_volume {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    auto* device = presentation.device();
    const char* vertex = R"(
float4 main(uint id:SV_VertexID):SV_Position {
    float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
    return float4(p[id],0,1);
})";
    std::string pixel(distance_fog_shader);
    pixel += R"(
cbuffer ForgeFogTest {float4 g_FogBase;float4 g_FogPoint;};
struct Output {float4 color:SV_Target0;float4 through:SV_Target1;};
Output main() {
    Output result;
    result.color=ForgeApplyDistanceFog(g_FogBase,g_FogPoint.xyz);
    result.through=float4(ForgeDistanceFogTransmittance(g_FogPoint.xyz),0,0,1);
    return result;
})";
    const std::array formats{TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_RGBA32_FLOAT};
    auto pass = ::pipeline(presentation, vertex, pixel.c_str(), formats, false);
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pass->CreateShaderResourceBinding(&binding, true);
    const auto base = ::buffer(device, 2 * sizeof(Row));
    const auto global = ::buffer(device, 2 * sizeof(Row));
    const auto local = ::buffer(device, sizeof(FogVolumeUniform));
    ::bind(binding, SHADER_TYPE_PIXEL, "ForgeFogTest", base);
    ::bind(binding, SHADER_TYPE_PIXEL, "ForgeDistanceFog", global);
    ::bind(binding, SHADER_TYPE_PIXEL, "ForgeFogVolumes", local);
    const auto color = ::texture(device, 1, 1, formats[0], BIND_RENDER_TARGET);
    const auto through = ::texture(device, 1, 1, formats[1], BIND_RENDER_TARGET);
    ITextureView* targets[]{color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                            through->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    double worst = 0;
    unsigned cases = 0;
    const auto run = [&](const char* label, std::span<const FogVolumeSample> volumes, Double3 point,
                         double transmission, Double3 medium, bool global_enabled = false,
                         bool diagnostic = false, const Double3* orthographic = nullptr) {
        const std::array<Row, 2> input{
            {{1, .25f, .5f, .35f}, {float(point[0]), float(point[1]), float(point[2]), 0}}};
        const std::array<Row, 2> fog{{{.8f, .3f, .1f, global_enabled ? 1.f : 0.f}, {0, 12, 0, 0}}};
        const auto values = fog_volume_uniform(volumes, orthographic);
        context->UpdateBuffer(base, 0, sizeof(input), input.data(),
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->UpdateBuffer(global, 0, sizeof(fog), fog.data(),
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->UpdateBuffer(local, 0, sizeof(values), values.data(),
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetRenderTargets(2, targets, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, 1, 1, 0, 1};
        context->SetViewports(1, &viewport, 1, 1);
        context->SetPipelineState(pass);
        context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawAttribs draw;
        draw.NumVertices = 3;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const auto actual = ::read_motion(device, context, color, 0, 0);
        const auto attenuated = ::read_motion(device, context, through, 0, 0);
        for (unsigned channel = 0; channel < 4; ++channel) {
            double expected = input[0][channel];
            if (diagnostic)
                expected = channel == 0 || channel >= 2 ? 1 : 0;
            else if (channel < 3) {
                if (global_enabled)
                    expected = expected * .5 + fog[0][channel] * .5;
                expected = expected * transmission + medium[channel] * (1 - transmission);
            }
            const double error = std::abs(double(actual[channel]) - expected);
            worst = std::max(worst, error);
            if (!std::isfinite(actual[channel]) || error > 2e-5 * std::max(1., std::abs(expected)))
                throw std::runtime_error(std::string("Fog Volume GPU color: ") + label +
                                         " channel " + std::to_string(channel) + " actual " +
                                         std::to_string(actual[channel]) + " expected " +
                                         std::to_string(expected));
        }
        const double expected_through = diagnostic ? 0 : transmission * (global_enabled ? .5 : 1);
        const double error = std::abs(double(attenuated[0]) - expected_through);
        worst = std::max(worst, error);
        if (!std::isfinite(attenuated[0]) || error > 2e-5)
            throw std::runtime_error(std::string("Fog Volume GPU transmittance: ") + label);
        ++cases;
    };
    FogVolume volume;
    volume.half_extent_x = volume.half_extent_y = volume.half_extent_z = 1;
    volume.density = .5f;
    volume.color_r = .2f;
    volume.color_g = .4f;
    volume.color_b = .8f;
    const Double3 medium{volume.color_r, volume.color_g, volume.color_b};
    AffineTransform world;
    world.m[11] = 3;
    std::array one{prepare_fog_volume(volume, world, {})};
    run("disabled", {}, {0, 0, 6}, 1, {});
    run("box through", one, {0, 0, 6}, std::exp(-1.), medium);
    run("box before", one, {0, 0, 1}, 1, medium);
    run("box surface inside", one, {0, 0, 3}, std::exp(-.5), medium);
    run("zero ray", one, {}, 1, medium);
    run("global composition", one, {0, 0, 6}, std::exp(-1.), medium, true);
    run("far surface finite", one, {0, 0, std::numeric_limits<float>::max()}, std::exp(-1.),
        medium);
    world.m[11] = 0;
    one[0] = prepare_fog_volume(volume, world, {});
    run("box camera inside", one, {0, 0, 6}, std::exp(-.5), medium);
    world.m[3] = 2;
    world.m[11] = 3;
    one[0] = prepare_fog_volume(volume, world, {});
    run("parallel miss", one, {0, 0, 6}, 1, medium);
    world.m[3] = 3;
    one[0] = prepare_fog_volume(volume, world, {});
    run("diagonal box", one, {6, 0, 6}, std::exp(-std::sqrt(2.)), medium);
    volume.shape = std::uint32_t(FogVolumeShape::Sphere);
    world.m[3] = .5;
    one[0] = prepare_fog_volume(volume, world, {});
    run("off axis sphere", one, {0, 0, 6}, std::exp(-std::sqrt(.75)), medium);
    const Double3 forward{0, 0, 1}, right{1, 0, 0};
    run("orthographic parallel sphere", one, {.5, 0, 6}, std::exp(-1.), medium, false, false,
        &forward);
    run("orthographic far lateral offset", one, {.5, 0, std::numeric_limits<float>::max()},
        std::exp(-1.), medium, false, false, &forward);
    world.m[3] = 3;
    world.m[11] = 0;
    one[0] = prepare_fog_volume(volume, world, {});
    run("orthographic rotated camera", one, {6, .5, 0}, std::exp(-std::sqrt(.75)), medium, false,
        false, &right);
    world.m[11] = 3;
    world.m[3] = 1;
    one[0] = prepare_fog_volume(volume, world, {});
    run("sphere tangent", one, {0, 0, 6}, 1, medium);
    world.m[3] = world.m[11] = 0;
    one[0] = prepare_fog_volume(volume, world, {});
    run("sphere camera inside", one, {0, 0, 6}, std::exp(-.5), medium);
    volume.shape = std::uint32_t(FogVolumeShape::Box);
    world = affine_transform({{0, 0, 3}, rotation_about_axis({0, 1, 0}, 45), {2, 1, .5f}});
    one[0] = prepare_fog_volume(volume, world, {});
    run("rotated nonuniform box", one, {0, 0, 6}, std::exp(-std::sqrt(.5)), medium);
    world = {};
    world.m[11] = 3;
    one[0] = prepare_fog_volume(volume, world, {});
    auto second = volume;
    second.density = 1;
    second.color_r = 1;
    second.color_g = .1f;
    second.color_b = .05f;
    std::array overlapping{one[0], prepare_fog_volume(second, world, {})};
    const Double3 mixed{(medium[0] + 2 * second.color_r) / 3, (medium[1] + 2 * second.color_g) / 3,
                        (medium[2] + 2 * second.color_b) / 3};
    run("overlap optical depth", overlapping, {0, 0, 6}, std::exp(-3.), mixed);
    std::reverse(overlapping.begin(), overlapping.end());
    run("overlap reversed", overlapping, {0, 0, 6}, std::exp(-3.), mixed);
    world.m[3] = world.m[7] = 1e12;
    world.m[11] = 1e12 + 3;
    one[0] = prepare_fog_volume(volume, world, {1e12, 1e12, 1e12});
    run("large origin", one, {0, 0, 6}, std::exp(-1.), medium);
    run("active invalid geometry", one, {0, 0, std::numeric_limits<double>::infinity()}, 0, {},
        false, true);
    world = {};
    world.m[11] = 3;
    volume.density = 0;
    one[0] = prepare_fog_volume(volume, world, {});
    run("zero density", one, {0, 0, 6}, 1, medium);
    volume.density = .005f;
    std::array<FogVolumeSample, fog_volume_limit> maximum;
    maximum.fill(prepare_fog_volume(volume, world, {}));
    run("maximum selection", maximum, {0, 0, 6}, std::exp(-.16), medium);
    volume.density = 1000;
    one[0] = prepare_fog_volume(volume, world, {});
    run("opaque density", one, {0, 0, 6}, 0, medium);
    context->WaitForIdle();
    std::cout << "Fog Volume production-helper GPU readback: " << cases
              << " analytic cases; worst absolute error " << worst << '\n';
}
} // namespace forge::test_fog_volume
