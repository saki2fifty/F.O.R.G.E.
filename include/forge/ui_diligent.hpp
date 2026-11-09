#pragma once
#include <cstdint>
#include <forge/asset_ref.hpp>
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
class UiPresenter;
class UiDiligentRenderer {
  public:
    explicit UiDiligentRenderer(Diligent::IRenderDevice*);
    ~UiDiligentRenderer();
    UiDiligentRenderer(const UiDiligentRenderer&) = delete;
    UiDiligentRenderer& operator=(const UiDiligentRenderer&) = delete;
    Rml::RenderInterface& render_interface();
    void begin(Diligent::IDeviceContext*, Diligent::ITextureView* color, unsigned width,
               unsigned height);
    void end();
    // Opaque editor preview with last-good publication. The returned view retains
    // its image independently of native context/backend retirement.
    std::shared_ptr<Diligent::ITextureView> draw_preview(UiPresenter&, Diligent::IDeviceContext*,
                                                         unsigned width, unsigned height);
    // Allocation counts and logical bytes; not physical GPU memory or timing.
    struct ClippingStats {
        std::uint64_t allocations = 0;
        unsigned cached_sizes = 0;
        std::uint64_t cached_bytes = 0;
    };
    ClippingStats clipping_stats() const;
    struct FilterStats {
        std::uint64_t allocations = 0, cached_bytes = 0;
        unsigned cached_images = 0, filters = 0, active_layers = 0;
    };
    FilterStats filter_stats() const;
    // Shared camera-output composition used by graphical hosts. The presenter
    // retains model/context/time ownership; the camera pass retains image ownership.
    void draw_output(UiPresenter&, AssetRef<RenderTextureAsset>, Diligent::IDeviceContext*,
                     Diligent::ITextureView*, unsigned width, unsigned height);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
