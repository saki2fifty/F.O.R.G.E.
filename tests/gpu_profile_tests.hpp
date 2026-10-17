#pragma once
#include "presentation_diligent.hpp"
#include <cmath>
#include <stdexcept>

// Shared native acceptance: D3D12 WARP and Vulkan execute this same lifecycle.
template <class Read>
void check_gpu_profiles(forge::DiligentPresentation& presentation,
                        Diligent::IDeviceContext* context, Read read) {
    using namespace Diligent;
    const auto check = [](bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    unsigned cpu_clock_reads = 0;
    forge::EngineServices services(false, [&] {
        ++cpu_clock_reads;
        return 0.;
    });
    presentation.gpu_services(services.access());
    TextureDesc desc;
    desc.Name = "FORGE GPU timing pixel preservation";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 8;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> texture;
    presentation.device()->CreateTexture(desc, nullptr, &texture);
    check(bool(texture), "GPU timing texture allocation failed");
    const auto clear = [&] {
        auto* target = texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float color[]{.2f, .4f, .7f, 1};
        context->ClearRenderTarget(target, color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    };
    const auto timed_clear = [&] {
        auto scope = presentation.gpu_scope(context, "Test", "Clear");
        clear();
    };
    // Synchronization below belongs to native test readback, never acquisition.
    const auto complete = [&] {
        presentation.end_frame(context);
        context->Flush();
        context->WaitForIdle();
        presentation.begin_frame();
    };
    presentation.begin_frame();
    timed_clear();
    const auto before = read(texture.RawPtr());
    check(presentation.gpu_profile_status().at("created") == 0,
          "Default-off GPU capture allocated FORGE queries");
    if (!presentation.gpu_profile_status().at("available").template get<bool>()) {
        bool rejected = false;
        try {
            presentation.gpu_profiling(true);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Unsupported GPU collection was enabled");
        presentation.gpu_services({});
        return;
    }
    presentation.gpu_profiling(true);
    unsigned foreign_rejections = 0;
    std::thread foreign([&] {
        try {
            presentation.gpu_profiling(false);
        } catch (...) {
            ++foreign_rejections;
        }
        try {
            presentation.gpu_profile_status();
        } catch (...) {
            ++foreign_rejections;
        }
        auto ignored = presentation.gpu_scope(context, "Test", "Foreign");
    });
    foreign.join();
    check(foreign_rejections == 2 && presentation.gpu_profile_status().at("created") == 0,
          "GPU profiler admitted a foreign-thread control or submitted a foreign query");
    for (unsigned i = 0; i < 64; ++i) {
        auto outer = presentation.gpu_scope(context, "Test", "Clear");
        auto nested = presentation.gpu_scope(context, "Test", "Nested");
        clear();
    }
    timed_clear(); // Pool full: drawing must still execute.
    auto status = presentation.gpu_profile_status();
    check(status.at("created") == 64 && status.at("pending") == 64 && status.at("skipped") == 65,
          "GPU query pool exceeded its bound or admitted nesting");
    presentation.gpu_profiling(false);
    presentation.gpu_profiling(true);
    complete();
    status = presentation.gpu_profile_status();
    check(status.at("pending") == 0 && status.at("stale") == 64 &&
              services.access().gpu_profiles().empty(),
          "Stop/restart published old in-flight GPU data");
    check(read(texture.RawPtr()) == before, "Enabling GPU queries changed rendered pixels");
    timed_clear();
    const auto expected_frame =
        presentation.gpu_profile_status().at("submitted_frame").template get<std::uint64_t>() + 1;
    complete();
    const auto samples = services.access().gpu_profiles();
    check(samples.size() == 1 && samples[0].at("clock") == "gpu" &&
              samples[0].at("frame") == expected_frame &&
              std::isfinite(samples[0].at("seconds").template get<double>()) &&
              samples[0].at("seconds") >= 0 && services.access().profiles().empty() &&
              cpu_clock_reads == 0,
          "Native GPU result lacks separate clock/frame provenance");
    check(presentation.gpu_profile_status().at("latency_frames") >= 1,
          "GPU sample did not disclose asynchronous frame latency");
    try {
        auto scope = presentation.gpu_scope(context, "Test", "Unwind");
        clear();
        throw std::runtime_error("Deliberate draw exception");
    } catch (const std::exception&) {
    }
    check(presentation.gpu_profile_status().at("pending") == 1,
          "Draw exception left an unbalanced native GPU scope");
    services.access().clear_profiles();
    complete();
    check(services.access().gpu_profiles().empty(), "Clear admitted unfinished native GPU data");
    timed_clear();
    forge::EngineServices replacement;
    presentation.gpu_services(replacement.access());
    check(!presentation.gpu_profile_status().at("enabled").template get<bool>(),
          "Rebinding did not stop GPU acquisition for the new owner/project");
    presentation.gpu_profiling(true);
    complete();
    check(replacement.access().gpu_profiles().empty(),
          "Rebind attributed old GPU data to new owner");
    timed_clear();
    complete();
    check(replacement.access().gpu_profiles().size() == 1 &&
              presentation.gpu_profile_status().at("created") == 64,
          "GPU pool failed to reuse completed query objects");
    {
        forge::EngineServices retired;
        presentation.gpu_services(retired.access());
        presentation.gpu_profiling(true);
        timed_clear();
    }
    complete();
    check(!presentation.gpu_profile_status().at("available").template get<bool>(),
          "GPU collector retained a retired service owner");
    presentation.gpu_services({});
}

template <class Draw>
void check_gpu_profile_draw(forge::DiligentPresentation& presentation,
                            Diligent::IDeviceContext* context, Draw draw, bool bloom) {
    const auto check = [](bool condition, const char* text) {
        if (!condition)
            throw std::runtime_error(text);
    };
    forge::EngineServices services;
    presentation.gpu_services(services.access());
    const auto baseline = draw();
    if (!presentation.gpu_profile_status().at("available").template get<bool>()) {
        presentation.gpu_services({});
        return;
    }
    presentation.begin_frame();
    presentation.gpu_profiling(true);
    check(draw() == baseline, "Profiling changed the actual composed Game pixels");
    presentation.end_frame(context);
    context->Flush();
    context->WaitForIdle(); // Test readback gate only; production acquisition never waits.
    presentation.begin_frame();
    bool geometry = false, display = false, glow = false;
    for (const auto& sample : services.access().gpu_profiles()) {
        check(sample.at("clock") == "gpu" && sample.at("category") == "Game",
              "Actual Game GPU timing was attributed to the wrong view/clock");
        geometry = geometry || sample.at("name") == "Camera geometry";
        display = display || sample.at("name") == "Display resolve";
        glow = glow || sample.at("name") == "Bloom";
    }
    check(geometry && display && glow == bloom && services.access().profiles().empty(),
          "Actual Game GPU pass instrumentation omitted a pass or included disabled Bloom");
    presentation.gpu_profiling(false);
    check(draw() == baseline, "Stopping profiling changed the actual composed Game pixels");
    presentation.gpu_services({});
}
