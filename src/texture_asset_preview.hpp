#pragma once
#include "gpu_residency.hpp"
#include "model_render_resource.hpp"
#include "texture_preview.hpp"
namespace forge {
// Small presentation-owned consumer of existing immutable asset revisions. The
// CPU pool performs verification asynchronously; GPU work stays on its owner.
class TextureAssetPreview {
  public:
    TextureAssetPreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                        std::filesystem::path project)
        : context_(context), project_(std::move(project)), pool_({1, 8, 16, 256ull * 1024 * 1024}),
          gpu_(presentation.device(), context, 256ull * 1024 * 1024, 16), renderer_(presentation) {}
    TexturePreviewSettings settings;
    void select(std::shared_ptr<const AssetCatalog> catalog, AssetRef<TextureAsset> asset,
                std::optional<TextureSemantic> semantic = {}) {
        if (catalog == catalog_ && asset == asset_ && semantic == semantic_)
            return;
        if (asset != asset_ || semantic != semantic_) {
            if (ticket_ || ready_cpu_)
                pool_.unload(asset_, ticket_ ? ticket_.inspect().identity.variant
                                             : ready_cpu_.identity().variant);
            ready_cpu_ = {};
            ready_gpu_ = {};
            settings = {};
            reset_display_ = true;
            output_valid_ = false;
        }
        catalog_ = std::move(catalog);
        asset_ = asset;
        semantic_ = semantic;
        error_.clear();
        ticket_ = {};
        attempted_.reset();
        try {
            ticket_ = asset_detail::request_texture(pool_, project_, catalog_, asset_, semantic_);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void cancel() {
        if (ticket_)
            pool_.cancel(asset_, ticket_.inspect().identity.variant);
        catalog_.reset();
    }
    void retry() {
        auto catalog = catalog_;
        catalog_.reset();
        select(std::move(catalog), asset_, semantic_);
    }
    void pump() {
        pool_.pump();
        gpu_.collect();
        if (!ticket_)
            return;
        const auto info = ticket_.inspect();
        if (info.state != ResourceState::Ready) {
            if (!info.diagnostic.empty())
                error_ = info.diagnostic;
            return;
        }
        auto candidate = pool_.current(asset_, info.identity.variant);
        if (!candidate || (ready_cpu_ && candidate.identity() == ready_cpu_.identity()) ||
            (attempted_ && *attempted_ == candidate.identity()))
            return;
        attempted_ = candidate.identity();
        try {
            auto native = gpu_.acquire(candidate);
            const auto& data = candidate.get();
            if (reset_display_) {
                settings.display =
                    data.semantic == TextureSemantic::HdrColor ? TexturePreviewDisplay::Hdr
                    : data.semantic == TextureSemantic::Color  ? TexturePreviewDisplay::Color
                                                               : TexturePreviewDisplay::Data;
                settings.signed_values = texture_format_info(data.format).signed_values;
                reset_display_ = false;
            }
            settings.alpha = data.alpha;
            settings.mip = std::min(settings.mip, data.mips - 1);
            settings.layer = std::min(settings.layer, data.layers - 1);
            const bool cube = data.dimension == TextureDimension::Cube ||
                              data.dimension == TextureDimension::CubeArray;
            settings.face = std::min(settings.face, cube ? 5u : 0u);
            settings.depth = std::min(settings.depth, std::max(1u, data.depth >> settings.mip) - 1);
            ready_cpu_ = std::move(candidate);
            ready_gpu_ = std::move(native);
            error_.clear();
            rendered_.reset();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    Diligent::ITextureView* render(unsigned width, unsigned height) {
        pump();
        if (!ready_gpu_)
            return nullptr;
        if (rendered_ && *rendered_ == settings && width == width_ && height == height_)
            return renderer_.output();
        try {
            auto* output = renderer_.render(context_, ready_gpu_.get(), settings, width, height);
            gpu_.submit();
            output_valid_ = true;
            rendered_ = settings;
            width_ = width;
            height_ = height;
            return output;
        } catch (const std::exception& e) {
            error_ = e.what();
            return output_valid_ ? renderer_.output() : nullptr;
        }
    }
    const TextureData* data() const { return ready_cpu_ ? &ready_cpu_.get() : nullptr; }
    std::uint64_t render_count() const { return renderer_.render_count(); }
    const std::string& error() const { return error_; }
    bool pending() const { return ticket_ && !resource_detail::terminal(ticket_.inspect().state); }
    const std::filesystem::path& project() const { return project_; }

  private:
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::filesystem::path project_;
    std::shared_ptr<const AssetCatalog> catalog_;
    AssetRef<TextureAsset> asset_;
    std::optional<TextureSemantic> semantic_;
    ResourcePool<TextureAsset> pool_;
    GpuResidency<TextureAsset> gpu_;
    ResourceTicket ticket_;
    ResourceLease<TextureAsset> ready_cpu_;
    GpuLease<TextureAsset> ready_gpu_;
    TexturePreviewRenderer renderer_; // Bindings die before leases and residency.
    std::optional<ResourceIdentity> attempted_;
    std::optional<TexturePreviewSettings> rendered_;
    unsigned width_ = 0, height_ = 0;
    bool reset_display_ = true, output_valid_ = false;
    std::string error_;
};
} // namespace forge
