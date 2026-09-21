#pragma once
#include "Components/interface/EnvMapRenderer.hpp"
#include "environment_gpu.hpp"
#include <forge/render_scene.hpp>
namespace forge {
// Exact upstream sky sampling and PSO; FORGE supplies camera rays and scene intent.
class EnvironmentSky {
  public:
    EnvironmentSky(DiligentPresentation&, Diligent::TEXTURE_FORMAT color);
    void select(const EnvironmentLease&);
    void draw(Diligent::IDeviceContext*, const CameraView&, const SceneEnvironment&);

  private:
    DiligentPresentation& presentation_;
    Diligent::TEXTURE_FORMAT color_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> camera_;
    EnvironmentLease source_;
    std::unique_ptr<Diligent::EnvMapRenderer> renderer_;
};
} // namespace forge
