#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "reflection_capture.hpp"
#include "reflection_capture_source.hpp"
#include <forge/engine_assets.hpp>
#include <iostream>
using namespace Diligent;
using namespace forge;
namespace {
void check(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void reject(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid reflection capture state/settings were admitted");
}
RenderScene scene(Double3 position) {
    constexpr std::array<Double3, 6> axes{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    RenderScene value;
    value.scene = AssetId::generate();
    value.settings.auto_exposure.enabled = true;
    value.settings.color_adjustments.enabled = true;
    value.settings.color_adjustments.filter = {.25f, 1, .5f};
    value.settings.bloom = {true, .2f, 1, 2};
    value.settings.ambient_occlusion.enabled = true;
    value.settings.screen_space_reflections.enabled = true;
    value.settings.temporal_anti_aliasing.enabled = true;
    value.settings.motion_blur.enabled = true;
    value.settings.depth_of_field.enabled = true;
    RenderReflectionProbe recursive;
    recursive.entity = EntityId::generate();
    recursive.probe.texture = {AssetId::generate()};
    value.reflection_probes.push_back(recursive);
    for (unsigned face = 0; face < axes.size(); ++face) {
        RenderMesh mesh;
        mesh.entity = EntityId::generate();
        mesh.renderer.mesh = engine_primitive(0);
        mesh.renderer.cast_shadows = false;
        mesh.renderer.layers = 1u << face;
        for (unsigned c = 0; c < 3; ++c) {
            mesh.world.m[c * 4 + c] = 2;
            mesh.world.m[c * 4 + 3] = position[c] + axes[face][c] * 5;
        }
        using T = MaterialParameterType;
        mesh.renderer.material_parameters = {
            {"surface", "baseColorFactor", {T::LinearColor4, {0, 0, 0, .25f}}},
            {"surface", "metallicFactor", {T::Scalar, {0}}},
            {"surface", "roughnessFactor", {T::Scalar, {1}}},
            {"surface",
             "emissiveFactor",
             {T::LinearColor3,
              {float(face % 6 + 1) / 8, float((face + 1) % 6 + 1) / 8,
               float((face + 2) % 6 + 1) / 8}}},
            {"surface", "emissiveStrength", {T::Scalar, {16}}}};
        value.meshes.push_back(std::move(mesh));
    }
    return value;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected Gallery project for shared resource ownership");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Reflection capture device unavailable");
        DiligentPresentation presentation(device);
        const auto root = std::filesystem::absolute(argv[1]);
        auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
        host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        const auto tick = [&](ReflectionCapture& capture) {
            presentation.begin_frame();
            capture.step(context);
            presentation.end_frame(context);
            host->submit();
            context->Flush();
            context->FinishFrame();
            device->ReleaseStaleResources();
            if (capture.state() == ReflectionCapture::State::Failed)
                throw std::runtime_error(capture.diagnostic());
        };
        const auto finish = [&](ReflectionCapture& capture) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
            while (capture.busy() && std::chrono::steady_clock::now() < deadline)
                tick(capture);
            check(!capture.busy(), "Reflection capture did not finish within the test deadline");
        };
        constexpr std::array<unsigned, 6> radiance{0x4000, 0x4400, 0x4600, 0x4800, 0x4900, 0x4a00};
        unsigned faces = 0;
        for (bool translated : {false, true})
            for (bool layers : {false, true}) {
                ReflectionCaptureSettings settings;
                settings.size = translated ? 64 : 33;
                settings.position = translated ? Double3{123456, -246810, 370368} : Double3{};
                settings.layers = layers ? 0x15 : UINT32_MAX;
                const auto input = scene(settings.position);
                const auto original_settings = input.settings;
                ReflectionCapture capture(presentation, host, input, settings,
                                          EntityId::generate());
                reject([&] { (void)capture.take(); });
                finish(capture);
                check(capture.state() == ReflectionCapture::State::Ready &&
                          capture.completed_faces() == 6 && !presentation.readback_pending(),
                      "Capture did not complete exactly six faces and retire its shared ticket");
                const auto cube = capture.take();
                validate_texture(cube);
                reject([&] { (void)capture.take(); });
                check(cube.dimension == TextureDimension::Cube &&
                          cube.format == TextureFormat::RGBA16Float &&
                          cube.semantic == TextureSemantic::HdrColor &&
                          cube.alpha == TextureAlpha::Opaque &&
                          input.settings == original_settings &&
                          input.reflection_probes.size() == 1,
                      "Capture changed input settings/probes or returned wrong source metadata");
                for (unsigned face = 0; face < 6; ++face) {
                    const auto& image = cube.subresources[face];
                    const auto center =
                        (std::size_t(settings.size / 2) * settings.size + settings.size / 2) * 8;
                    for (unsigned c = 0; c < 4; ++c) {
                        const auto at = center + c * 2;
                        const auto bits = std::to_integer<unsigned>(image[at]) |
                                          (std::to_integer<unsigned>(image[at + 1]) << 8);
                        const unsigned expected = c == 3               ? 0x3c00
                                                  : layers && face % 2 ? 0
                                                                       : radiance[(face + c) % 6];
                        check(bits == expected, "Captured face lost HDR radiance, world position, "
                                                "layer mask or opaque alpha");
                    }
                    ++faces;
                }
                check(!encode_reflection_capture_source(cube).empty(),
                      "Captured cube cannot enter the established DDS texture-source path");
            }
        ReflectionCaptureSettings settings;
        settings.size = 32;
        const auto input = scene({});
        for (unsigned bad : {0u, 15u, 1025u}) {
            auto invalid = settings;
            invalid.size = bad;
            reject([&] {
                ReflectionCapture capture(presentation, host, input, invalid, EntityId::generate());
            });
        }
        ReflectionCapture cancelled(presentation, host, input, settings, EntityId::generate());
        while (cancelled.state() == ReflectionCapture::State::Capturing)
            tick(cancelled);
        check(cancelled.state() == ReflectionCapture::State::Readback,
              "Cancellation fixture did not own a submitted HDR copy");
        cancelled.cancel();
        finish(cancelled);
        check(cancelled.state() == ReflectionCapture::State::Cancelled &&
                  !presentation.readback_pending(),
              "Cancelled capture returned data or did not retire its shared native ticket");
        reject([&] { (void)cancelled.take(); });
        ReflectionCapture changed(presentation, host, input, settings, EntityId::generate());
        tick(changed);
        host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        finish(changed);
        check(changed.state() == ReflectionCapture::State::Cancelled &&
                  changed.diagnostic().find("published assets changed") != std::string::npos &&
                  !presentation.readback_pending(),
              "Catalog replacement mixed capture revisions or lost cancellation retirement");
        context->WaitForIdle();
        std::cout << faces
                  << " actual scoped reflection capture faces PASS; translated origin, "
                     "layers, HDR/opaque alpha, recursive/post bypass, take-once, "
                     "cancellation/catalog retirement\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
