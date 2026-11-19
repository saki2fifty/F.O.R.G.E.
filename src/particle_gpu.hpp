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
    ParticleGpuInstances(const ParticleGpuInstances&) = delete;
    ParticleGpuInstances& operator=(const ParticleGpuInstances&) = delete;
    bool supported() const;
    // Compute unbinds render targets. The camera composition owner restores them
    // before drawing. Success publishes only this submission's instance count.
    void evaluate(Diligent::IDeviceContext*, const ParticleGpuValues&);
    Diligent::IBuffer* buffer() const { return output_; }
    unsigned count() const { return count_; }

  private:
    void prepare();
    DiligentPresentation& presentation_;
    unsigned capacity_, count_ = 0;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_, output_;
};
} // namespace forge
