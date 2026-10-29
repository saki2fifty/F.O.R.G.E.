#pragma once
#include "ambient_occlusion.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include <forge/render_view.hpp>
#include <iostream>
#include <limits>
#include <thread>

namespace forge::test_occlusion {
inline void boundaries(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    const std::vector<test_bloom::Pixel> solid(9 * 7, {.5f, .25f, .125f, .75f});
    auto color = test_bloom::texture(device, 9, 7, solid);
    auto output = test_bloom::texture(device, 9, 7, solid);
    auto other = test_bloom::texture(device, 10, 7, std::vector<test_bloom::Pixel>(70));
    TextureDesc desc;
    desc.Name = "FORGE ambient input admission fixture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 9;
    desc.Height = 7;
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> depth, wrong, mipped;
    device->CreateTexture(desc, nullptr, &depth);
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &wrong);
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.MipLevels = 2;
    device->CreateTexture(desc, nullptr, &mipped);
    check(depth && wrong && mipped, "Ambient admission fixture allocation failed");
    AmbientOcclusion effect(presentation);
    AmbientOcclusion::Input input;
    input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    auto* target = output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    const auto misses = presentation.cache_misses();
    effect.apply(context, input, target);
    check(presentation.cache_misses() == misses &&
              test_bloom::read(device, context, output) == solid,
          "Disabled ambient path compiled a pipeline or required depth/indirect inputs");
    input.indirect = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.settings.enabled = true;
    Camera settings;
    input.projection = camera_view(settings, {}, 9, 7).projection;
    const auto rejected = [&](auto alter) {
        auto candidate = input;
        alter(candidate);
        bool failed = false;
        try {
            effect.apply(context, candidate, target);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Ambient operator admitted invalid typed inputs or projection");
    };
    rejected([&](auto& v) { v.color = wrong->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE); });
    rejected([&](auto& v) { v.color = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET); });
    rejected([&](auto& v) { v.color = mipped->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE); });
    rejected([&](auto& v) { v.indirect = output->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE); });
    rejected([&](auto& v) { v.indirect = other->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE); });
    rejected([&](auto& v) { v.depth = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL); });
    rejected([](auto& v) { v.indirect = nullptr; });
    rejected([](auto& v) { v.projection[0] = 0; });
    rejected([](auto& v) { v.projection[1] = .1f; });
    rejected([](auto& v) { v.projection[10] = std::numeric_limits<float>::infinity(); });
    rejected([](auto& v) { v.projection[14] = v.projection[15] = 0; });
    rejected([](auto& v) {
        v.settings.enabled = false;
        v.settings.bias = 2;
    });
    bool thread_rejected = false;
    std::thread other_thread([&] {
        try {
            effect.apply(context, input, target);
        } catch (const std::invalid_argument&) {
            thread_rejected = true;
        }
    });
    other_thread.join();
    check(thread_rejected, "Ambient operator accepted a foreign presentation thread");
    input.settings.strength = 0;
    effect.apply(context, input, target);
    check(presentation.cache_misses() == misses &&
              test_bloom::read(device, context, output) == solid,
          "Rejected ambient input prevented the allocation-free zero path from recovering");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->WaitForIdle();
    std::cout
        << "Ambient typed target, projection, thread and pipeline-free bypass boundaries PASS\n";
}
} // namespace forge::test_occlusion
