#pragma once
#include "../reflection_capture.hpp"
#include "reflection_capture_task.hpp"
namespace forge::ui {
// Native bridge for the one image task; no independent frame submission or IO.
class ReflectionCaptureHost final : public ReflectionCaptureDriver {
  public:
    ReflectionCaptureHost(DiligentPresentation& presentation,
                          std::shared_ptr<MeshResourceHost> resources, RenderScene snapshot,
                          const ReflectionCaptureSettings& settings, EntityId probe)
        : capture_(presentation, std::move(resources), std::move(snapshot), settings, probe) {}
    void step(Diligent::IDeviceContext* context) override { capture_.step(context); }
    void cancel(const std::string& reason) override { capture_.cancel(reason); }
    bool busy() const override { return capture_.busy(); }
    unsigned completed_faces() const override { return capture_.completed_faces(); }
    std::string diagnostic() const override { return capture_.diagnostic(); }
    TextureData take() override { return capture_.take(); }

  private:
    ReflectionCapture capture_;
};
} // namespace forge::ui
