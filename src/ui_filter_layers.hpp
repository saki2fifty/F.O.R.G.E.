#pragma once
#include <RmlUi/Core/RenderInterface.h>
#include <cstdint>
#include <memory>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
struct ITexture;
} // namespace Diligent
namespace forge {
// Private facet of UiDiligentRenderer. These are transient UI drawing images,
// never Scene outputs, Content identities or another UI resource service.
class UiFilterLayers {
  public:
    explicit UiFilterLayers(Diligent::IRenderDevice*);
    ~UiFilterLayers();
    void begin(Diligent::IDeviceContext*, Diligent::ITextureView*, unsigned, unsigned);
    void end();
    Rml::CompiledFilterHandle compile(const Rml::String&, const Rml::Dictionary&);
    void release(Rml::CompiledFilterHandle);
    Rml::CompiledFilterHandle save_mask();
    struct SavedTexture {
        Diligent::ITexture* texture = nullptr;
        std::uintptr_t token = 0;
    };
    SavedTexture save_texture(Rml::Rectanglei);
    void release_texture(std::uintptr_t);
    Rml::LayerHandle push();
    void pop();
    Diligent::ITextureView* current_color() const;
    void composite(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode,
                   Rml::Span<const Rml::CompiledFilterHandle>, Diligent::ITextureView* stencil,
                   unsigned stencil_ref, bool clip, bool scissor, Rml::Rectanglei);
    struct Statistics {
        std::uint64_t allocations = 0, cached_bytes = 0;
        unsigned cached_images = 0, filters = 0, active_layers = 0;
    };
    Statistics statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
