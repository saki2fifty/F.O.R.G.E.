#pragma once
#include <memory>
namespace Rml {
class RenderInterface;
}
namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
} // namespace Diligent
namespace forge {
class UiDiligentRenderer {
  public:
    explicit UiDiligentRenderer(Diligent::IRenderDevice*);
    ~UiDiligentRenderer();
    UiDiligentRenderer(const UiDiligentRenderer&) = delete;
    UiDiligentRenderer& operator=(const UiDiligentRenderer&) = delete;
    Rml::RenderInterface& interface();
    void begin(Diligent::IDeviceContext*, Diligent::ITextureView* color, unsigned width,
               unsigned height);
    void end();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
