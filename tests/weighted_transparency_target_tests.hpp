#pragma once
#include "presentation_views.hpp"
#include "weighted_transparency.hpp"
namespace forge::test_weighted {
inline void targets(forge::DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    Diligent::ITexture* color, Diligent::ITextureView* depth) {
    using namespace Diligent;
    using test_bloom::check;
    const auto& desc = color->GetDesc();
    const forge::PixelViewport area{0, 0, desc.Width, desc.Height};
    const auto rejected = [](auto operation) {
        bool failed = false;
        try {
            operation();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Invalid weighted transparency target was admitted");
    };
    check(forge::complete_surface_view(depth, TEX_FORMAT_D32_FLOAT, TEXTURE_VIEW_DEPTH_STENCIL),
          "Complete native D32 attachment rejected");
    check(
        !forge::complete_surface_view(depth, TEX_FORMAT_D32_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE) &&
            !forge::complete_surface_view(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                          TEX_FORMAT_RGBA32_FLOAT, TEXTURE_VIEW_DEPTH_STENCIL),
        "Target helper confused depth and color view roles");
    rejected([&] { forge::WeightedTransparency pass(presentation, 0); });
    forge::WeightedTransparency pass(presentation, std::uint64_t(desc.Width) * desc.Height * 24);
    rejected([&] { pass.prepare(nullptr, depth, area); });
    rejected([&] { pass.prepare(color, nullptr, area); });
    rejected([&] { pass.prepare(color, depth, {0, 0, 0, desc.Height}); });
    rejected([&] { pass.prepare(color, depth, {desc.Width, 0, 1, 1}); });
    rejected([&] { pass.begin(nullptr, depth); });
    pass.prepare(color, depth, area);
    const auto before = test_bloom::read(presentation.device(), context, color);
    // A failed replacement must retain the usable target, viewport and bindings.
    rejected([&] { pass.prepare(color, depth, {0, 0, desc.Width + 1, desc.Height}); });
    forge::WeightedTransparency too_small(presentation,
                                          std::uint64_t(desc.Width) * desc.Height * 24 - 1);
    rejected([&] { too_small.prepare(color, depth, area); });
    pass.begin(context, depth);
    pass.composite(context, color, depth);
    check(test_bloom::read(presentation.device(), context, color) == before,
          "Empty accumulation or rejected target replacement changed HDR pixels");
    pass.clear();
    rejected([&] { pass.composite(context, color, depth); });
    pass.prepare(color, depth, area);
    pass.begin(context, depth);
    pass.composite(context, color, depth);
    check(test_bloom::read(presentation.device(), context, color) == before,
          "Weighted attachments failed clear/recreate recovery");
    context->FinishFrame();
    presentation.device()->ReleaseStaleResources();
}
} // namespace forge::test_weighted
