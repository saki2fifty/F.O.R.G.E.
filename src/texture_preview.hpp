#pragma once
#include "presentation_diligent.hpp"
#include <forge/texture_asset.hpp>
namespace forge {
enum class TexturePreviewChannel { Rgba, Rgb, Red, Green, Blue, Alpha };
enum class TexturePreviewDisplay { Data, Color, Hdr };
struct TexturePreviewSettings {
    unsigned mip = 0, layer = 0, face = 0, depth = 0;
    TexturePreviewChannel channel = TexturePreviewChannel::Rgba;
    TexturePreviewDisplay display = TexturePreviewDisplay::Color;
    float exposure = 0;
    bool checker = true, nearest = false, signed_values = false;
    TextureAlpha alpha = TextureAlpha::Straight;
    std::array<float, 4> region{0, 0, 1, 1}; // Normalized crop, minimum then maximum.
    bool operator==(const TexturePreviewSettings&) const = default;
};
// Presentation-only view of admitted GPU textures. No source decoding, persistent
// identities or ImGui dependencies. The caller retains its source resource lease
// through submission and this output owner through the consuming UI submission.
class TexturePreviewRenderer {
  public:
    explicit TexturePreviewRenderer(DiligentPresentation&);
    Diligent::ITextureView* render(Diligent::IDeviceContext*, Diligent::ITexture*,
                                   const TexturePreviewSettings&, unsigned width, unsigned height);
    Diligent::ITextureView* output() const;
    std::uint64_t render_count() const { return renders_; }

  private:
    DiligentPresentation& presentation_;
    std::uint64_t renders_ = 0;
    struct Pipeline {
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> state;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding;
    };
    std::array<Pipeline, 3> pipelines_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> output_;
    Pipeline& pipeline(unsigned dimension);
};
} // namespace forge
