#pragma once
#include "particle_gpu_plan.hpp"
#include "presentation_diligent.hpp"

namespace forge {
// One renderer-owned emitter realization. Presentation retains the shared native
// program cache; the emitter retains its bounded buffers and binding. No clock,
// source registry, World pointer or CPU particle state lives here.
class ParticleGpuInstances {
  public:
    ParticleGpuInstances(DiligentPresentation&, unsigned capacity);
    // The Shader selection owner supplies an already realized immutable program.
    ParticleGpuInstances(DiligentPresentation&, unsigned capacity, Diligent::IShader*,
                         const ParticleGpuGraphLimits&);
    ParticleGpuInstances(const ParticleGpuInstances&) = delete;
    ParticleGpuInstances& operator=(const ParticleGpuInstances&) = delete;
    bool supported() const;
    // Compute unbinds render targets. The camera composition owner restores them
    // before drawing. Success publishes only this submission's instance count.
    void evaluate(Diligent::IDeviceContext*, const ParticleGpuValues&,
                  const CameraView* billboard_view = nullptr);
    Diligent::IBuffer* buffer() const { return output_; }
    unsigned count() const { return count_; }

  private:
    void prepare();
    void prepare_billboards();
    DiligentPresentation& presentation_;
    unsigned capacity_, count_ = 0;
    Diligent::RefCntAutoPtr<Diligent::IShader> graph_program_;
    ParticleGpuGraphLimits graph_limits_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> graph_values_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_, output_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> billboard_pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> billboard_binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> billboard_values_;
};
} // namespace forge
