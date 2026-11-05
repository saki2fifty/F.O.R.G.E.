#pragma once
#include "asset_bytes.hpp"
#include "paint_texture_source.hpp"
#include "texture_asset_preview.hpp"

namespace forge {
// Adapt an immutable source value to the existing texture preview owners. CPU
// pixels prepare asynchronously; this adapter has no asset publication authority.
class PaintTexturePreview {
  public:
    PaintTexturePreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                        std::filesystem::path project)
        : preview_(presentation, context, std::move(project)) {}
    const std::filesystem::path& project() const { return preview_.project(); }
    bool current() const { return preview_.current(); }
    const std::string& error() const { return preview_.error(); }
    Diligent::ITextureView* render(const PaintTextureValue& source, std::uint64_t session,
                                   std::uint64_t revision, unsigned width, unsigned height) {
        if (asset_ != source.asset() || session_ != session || revision_ != revision) {
            if (generation_ == UINT64_MAX)
                throw std::runtime_error("Paint preview generation exhausted");
            const auto key = std::to_string(session) + ":" + std::to_string(revision);
            preview_.select_draft(
                {source.asset()}, asset_detail::content_digest(std::as_bytes(std::span(key))),
                ++generation_, [source](std::stop_token stop) {
                    if (stop.stop_requested())
                        throw std::runtime_error("Paint preview cancelled");
                    auto pixels = std::make_unique<TextureData>(source.texture());
                    const auto bytes = pixels->byte_size();
                    return ResourceCandidate<TextureAsset>{std::move(pixels), {bytes}};
                });
            asset_ = source.asset();
            session_ = session;
            revision_ = revision;
        }
        preview_.settings.nearest = true;
        return preview_.render(width, height);
    }

  private:
    TextureAssetPreview preview_;
    AssetId asset_;
    std::uint64_t session_ = 0, revision_ = 0, generation_ = 0;
};
} // namespace forge
