#pragma once
#include "hdr_bloom_gpu_tests.hpp"
#include "screen_space_reflections.hpp"
#include <forge/render_view.hpp>
#include <limits>
#include <thread>
namespace forge::test_reflection {
inline void boundaries(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    const std::vector<test_bloom::Pixel> color_values(63, {.5f, .25f, .125f, .75f});
    const std::vector<test_bloom::Pixel> indirect_values(63, {.25f, .125f, .0625f, .25f});
    auto color = test_bloom::texture(device, 9, 7, color_values);
    auto indirect = test_bloom::texture(device, 9, 7, indirect_values);
    auto output = test_bloom::texture(device, 9, 7, color_values);
    auto indirect_output = test_bloom::texture(device, 9, 7, indirect_values);
    auto larger = test_bloom::texture(device, 10, 7, std::vector<test_bloom::Pixel>(70));
    TextureDesc d;
    d.Name = "FORGE reflection admission depth";
    d.Type = RESOURCE_DIM_TEX_2D;
    d.Width = 9;
    d.Height = 7;
    d.Format = TEX_FORMAT_D32_FLOAT;
    d.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> depth;
    device->CreateTexture(d, nullptr, &depth);
    check(bool(depth), "Reflection admission depth allocation failed");
    ScreenSpaceReflections effect(presentation);
    ScreenSpaceReflections::Input input;
    input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.indirect = indirect->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    auto* result = output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* result_indirect = indirect_output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    const auto misses = presentation.cache_misses();
    effect.apply(context, input, result, result_indirect);
    check(presentation.cache_misses() == misses &&
              test_bloom::read(device, context, output) == color_values &&
              test_bloom::read(device, context, indirect_output) == indirect_values,
          "Disabled reflections compiled a pipeline, required materials or changed current inputs");
    input.settings.enabled = true;
    input.surface = input.response = input.specular =
        color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.projection = camera_view(Camera{}, {}, 9, 7).projection;
    const auto reject = [&](auto alter) {
        auto candidate = input;
        auto* a = result;
        auto* b = result_indirect;
        bool failed = false;
        alter(candidate, a, b);
        try {
            effect.apply(context, candidate, a, b);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Reflection operator admitted invalid role/type/extent/alias/projection");
    };
    reject([&](auto& v, auto*&, auto*&) { v.surface = input.depth; });
    reject([&](auto& v, auto*&, auto*&) { v.response = input.depth; });
    reject([&](auto& v, auto*&, auto*&) { v.specular = input.depth; });
    reject([&](auto& v, auto*&, auto*&) { v.depth = input.color; });
    reject([&](auto& v, auto*&, auto*&) {
        v.surface = larger->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    });
    reject([&](auto& v, auto*&, auto*&) {
        v.color = output->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    });
    reject([&](auto&, auto*& a, auto*& b) { b = a; });
    reject(
        [&](auto&, auto*& a, auto*&) { a = output->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE); });
    reject([](auto& v, auto*&, auto*&) { v.surface = nullptr; });
    reject([](auto& v, auto*&, auto*&) { v.projection[5] = 0; });
    reject([](auto& v, auto*&, auto*&) { v.projection[1] = .1f; });
    reject([](auto& v, auto*&, auto*&) {
        v.projection[10] = std::numeric_limits<float>::quiet_NaN();
    });
    reject([](auto& v, auto*&, auto*&) {
        v.settings.enabled = false;
        v.settings.thickness = 0;
    });
    bool thread_rejected = false;
    std::thread other([&] {
        try {
            effect.apply(context, input, result, result_indirect);
        } catch (const std::invalid_argument&) {
            thread_rejected = true;
        }
    });
    other.join();
    check(thread_rejected, "Reflection operator accepted a foreign presentation thread");
    input.settings.max_distance = 0;
    input.surface = input.depth = input.response = input.specular = nullptr;
    effect.apply(context, input, result, result_indirect);
    check(presentation.cache_misses() == misses &&
              test_bloom::read(device, context, output) == color_values &&
              test_bloom::read(device, context, indirect_output) == indirect_values,
          "Reflection validation failure prevented allocation-free zero-path recovery");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->WaitForIdle();
    std::cout << "Reflection role/type/alias/extent/projection/thread and pipeline-free retry "
                 "boundaries PASS\n";
}
} // namespace forge::test_reflection
