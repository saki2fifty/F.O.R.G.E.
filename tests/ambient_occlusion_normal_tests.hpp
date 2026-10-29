#pragma once
#include "ambient_occlusion.hpp"
#include "ambient_occlusion_numeric_tests.hpp"
#include "hdr_bloom_gpu_tests.hpp"
namespace forge::test_occlusion {
inline unsigned normal_inputs(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                              AmbientOcclusion& effect, AmbientOcclusion::Input input,
                              Diligent::ITextureView* output,
                              const std::vector<test_bloom::Pixel>& original,
                              const std::vector<test_bloom::Pixel>& geometric, bool has_occluder) {
    using namespace Diligent;
    using test_bloom::check;
    const auto& size = input.color->GetTexture()->GetDesc();
    auto* device = presentation.device();
    const test_bloom::Pixel normals[]{
        {0, 0, -1, 0}, {.8f, 0, -.6f, 0}, {-.8f, 0, -.6f, 0},
        {0, 0, 0, 0},  {0, 0, 1, 0},      {std::numeric_limits<float>::quiet_NaN(), 0, -1, 0}};
    double integrated[6][3]{};
    unsigned frames = 0;
    for (unsigned normal = 0; normal < std::size(normals); ++normal) {
        auto surface = test_bloom::texture(
            device, size.Width, size.Height,
            std::vector<test_bloom::Pixel>(std::size_t(size.Width) * size.Height, normals[normal]));
        input.surface = surface->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        for (unsigned blend = 0; blend < 3; ++blend) {
            input.settings.normal_influence = .5f * blend;
            presentation.begin_frame();
            effect.apply(context, input, output);
            presentation.end_frame(context);
            const auto actual = test_bloom::read(device, context, output->GetTexture());
            if (has_occluder && size.Width == 97 && input.projection[14] == 0 &&
                input.projection[3] == 0 && input.projection[7] == 0 && normal < 3 &&
                (normal != 0 || blend == 0)) {
                const double amount = .5 * blend;
                numeric_reference(actual, size.Width, size.Height, input.projection[0],
                                  amount * normals[normal][0],
                                  -(1 - amount) + amount * normals[normal][2]);
            }
            for (std::size_t i = 0; i < actual.size(); ++i) {
                check(actual[i][3] == original[i][3], "Material-normal AO changed current alpha");
                if (!blend || normal == 0 || normal >= 3)
                    check(actual[i] == geometric[i],
                          "Default, matching or invalid normal changed geometric occlusion");
                if (!has_occluder)
                    check(actual[i] == original[i],
                          "Material normal shaded a flat plane or direct-only lighting");
                for (unsigned c = 0; c < 3; ++c) {
                    constexpr float radiance[]{.5f, .25f, .125f};
                    check(std::isfinite(actual[i][c]) && actual[i][c] <= original[i][c] &&
                              actual[i][c] >= original[i][c] - radiance[c] - .0001f,
                          "Material-normal AO exceeded available indirect light");
                }
                integrated[normal][blend] += original[i][0] - actual[i][0];
            }
            context->FinishFrame();
            device->ReleaseStaleResources();
            ++frames;
        }
    }
    if (has_occluder)
        check(integrated[1][2] > integrated[2][2] + .5,
              "Right-hand occluder did not respond to receiver normal direction");
    input.settings.normal_influence = 1;
    for (auto* wrong :
         {static_cast<ITextureView*>(nullptr), input.color, input.indirect, input.depth, output}) {
        input.surface = wrong;
        bool rejected = false;
        try {
            effect.apply(context, input, output);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "Normal occlusion admitted a missing, aliased or mistyped surface");
    }
    input.surface = nullptr;
    input.settings.enabled = false;
    presentation.begin_frame();
    effect.apply(context, input, output);
    presentation.end_frame(context);
    check(test_bloom::read(device, context, output->GetTexture()) == original,
          "Disabled normal occlusion required a normal or changed the view");
    context->FinishFrame();
    device->ReleaseStaleResources();
    return frames;
}
} // namespace forge::test_occlusion
