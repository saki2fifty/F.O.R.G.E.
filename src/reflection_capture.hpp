#pragma once
#include "frame_renderer.hpp"
#include "reflection_capture_settings.hpp"
#include <chrono>
namespace forge {
// One scoped image-task job. FrameRenderer owns its view images and the existing
// presentation owner retains the single readback ticket. No asset IO/publication,
// world, persistent identity, independent image cache or simulation clock here.
class ReflectionCapture {
  public:
    enum class State { Capturing, Readback, Ready, Draining, Cancelled, Failed, Taken };
    ReflectionCapture(DiligentPresentation&, std::shared_ptr<MeshResourceHost>, RenderScene,
                      const ReflectionCaptureSettings&, EntityId);
    ~ReflectionCapture();
    ReflectionCapture(const ReflectionCapture&) = delete;
    ReflectionCapture& operator=(const ReflectionCapture&) = delete;
    // Called within the host's existing presentation frame, before end_frame.
    void step(Diligent::IDeviceContext*);
    void cancel(std::string reason = "Reflection capture cancelled");
    State state() const { return state_; }
    unsigned completed_faces() const { return face_; }
    const std::string& diagnostic() const { return diagnostic_; }
    bool busy() const {
        return state_ == State::Capturing || state_ == State::Readback || state_ == State::Draining;
    }
    TextureData take();

  private:
    void stop(State, std::string);
    DiligentPresentation& presentation_;
    const std::thread::id thread_ = std::this_thread::get_id();
    std::shared_ptr<MeshResourceHost> resources_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::unique_ptr<FrameRenderer> renderer_;
    RenderScene scene_;
    std::array<PreparedCamera, 6> cameras_;
    TextureData cube_;
    State state_ = State::Capturing, terminal_ = State::Cancelled;
    unsigned face_ = 0;
    bool owns_readback_ = false;
    std::string diagnostic_;
    std::chrono::steady_clock::time_point deadline_;
};
} // namespace forge
