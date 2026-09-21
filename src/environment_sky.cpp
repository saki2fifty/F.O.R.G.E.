#include "environment_sky.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "sky_view.hpp"
#include <cmath>
#include <numbers>
namespace Diligent::HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
} // namespace Diligent::HLSL
namespace forge {
using namespace Diligent;
EnvironmentSky::EnvironmentSky(DiligentPresentation& presentation, TEXTURE_FORMAT color)
    : presentation_(presentation), color_(color) {
    BufferDesc desc;
    desc.Name = "FORGE camera-relative sky rays";
    desc.Size = sizeof(HLSL::CameraAttribs) * 2;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    presentation.device()->CreateBuffer(desc, nullptr, &camera_);
    if (!camera_)
        throw std::runtime_error("Sky camera buffer allocation failed");
}
void EnvironmentSky::select(const EnvironmentLease& source) {
    if ((!source && !source_) || (source && source_ && source.source() == source_.source()))
        return;
    // Native renderer retains separate cube/sphere bindings. Recreate those
    // bindings before dropping the old lease when switching source dimensions.
    if (!source) {
        renderer_.reset();
        source_ = {};
        return;
    }
    EnvMapRenderer::CreateInfo info;
    info.pDevice = presentation_.device();
    info.pStateCache = presentation_.cache();
    info.pCameraAttribsCB = camera_;
    info.RTVFormats[0] = color_;
    info.DSVFormat = TEX_FORMAT_D32_FLOAT;
    info.PackMatrixRowMajor = true;
    auto candidate = std::make_unique<EnvMapRenderer>(info);
    renderer_ = std::move(candidate);
    source_ = source;
}
void EnvironmentSky::draw(IDeviceContext* context, const CameraView& view,
                          const SceneEnvironment& settings) {
    if (!source_ || !renderer_ || !settings.sky)
        return;
    if (!context || !std::isfinite(settings.intensity) || settings.intensity < 0 ||
        !std::isfinite(settings.rotation))
        throw std::runtime_error("Invalid sky context or authored lighting values");
    const auto rays = sky_ray_matrix(view);
    {
        MapHelper<HLSL::CameraAttribs> data(context, camera_, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = {};
        data[0].fFarPlaneDepth = 1;
        data[0].f4Position = {0, 0, 0, 1};
        data[0].f4ViewportSize = {float(view.viewport.width), float(view.viewport.height),
                                  1.f / view.viewport.width, 1.f / view.viewport.height};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned col = 0; col < 4; ++col)
                data[0].mViewProjInv[row][col] = rays[row * 4 + col];
        data[1] = data[0];
    }
    EnvMapRenderer::RenderAttribs args;
    args.pEnvMap = source_.get().source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    args.Scale = {settings.intensity, settings.intensity, settings.intensity};
    // Reduce in double before native float yaw; identical periodic orientation to IBL.
    args.Yaw = float(std::remainder(settings.rotation, 2 * std::numbers::pi));
    args.SphereMapRow0IsNegativeY = false;
    HLSL::ToneMappingAttribs tone;
    tone.iToneMappingMode = TONE_MAPPING_MODE_NONE;
    renderer_->Prepare(context, args, tone);
    renderer_->Render(context);
}
} // namespace forge
