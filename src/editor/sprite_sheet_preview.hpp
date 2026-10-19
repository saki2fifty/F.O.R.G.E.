#pragma once
#include "../sprite_material_profile.hpp"
#include "../texture_asset_preview.hpp"

namespace forge {
// One disposable view of the existing published Material -> Texture selection.
// Existing resource factories/pools own verification, cancellation and revisions;
// this controller owns no authored component, asset record or source parser.
class SpriteSheetPreview {
  public:
    SpriteSheetPreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                       std::filesystem::path project)
        : project_(std::move(project)),
          texture_(presentation, context, project_, validate_sprite_texture_profile) {}
    const std::filesystem::path& project() const { return project_; }
    void select(std::shared_ptr<const AssetCatalog> catalog, AssetRef<MaterialAsset> material) {
        if (catalog == catalog_ && material == material_)
            return;
        if (material != material_ && ticket_)
            materials_.unload(material_, ticket_.inspect().identity.variant);
        catalog_ = std::move(catalog);
        material_ = material;
        ready_material_ = {};
        attempted_material_.reset();
        ticket_ = {};
        ready_ = false;
        error_.clear();
        try {
            if (!catalog_ || !material.id)
                throw std::runtime_error("Assign a Sprite image material first.");
            ticket_ =
                asset_detail::request_model_pbr_material(materials_, project_, catalog_, material);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void cancel() {
        if (ticket_)
            materials_.cancel(material_, ticket_.inspect().identity.variant);
        texture_.cancel();
        ready_material_ = {};
        ticket_ = {};
        catalog_.reset();
        ready_ = false;
    }
    void retry() {
        auto catalog = catalog_;
        catalog_.reset();
        texture_.retry();
        select(std::move(catalog), material_);
    }
    void pump() {
        ready_ = false;
        materials_.pump();
        materials_.collect();
        if (!ticket_)
            return;
        const auto state = ticket_.inspect();
        if (state.state != ResourceState::Ready) {
            if (!state.diagnostic.empty())
                error_ = state.diagnostic;
            return;
        }
        try {
            auto candidate = materials_.current(material_, state.identity.variant);
            if (!candidate || candidate.identity() != state.identity)
                return;
            if (!ready_material_ || ready_material_.identity() != candidate.identity()) {
                if (attempted_material_ && *attempted_material_ == candidate.identity())
                    return;
                attempted_material_ = candidate.identity();
                if (candidate->surface)
                    throw std::runtime_error(
                        "Choose a Sprite image material without a custom surface.");
                validate_render_material(candidate.get());
                validate_sprite_material_profile(candidate->values);
                ready_material_ = std::move(candidate);
            }
            texture_.select(catalog_, ready_material_->textures.at("baseColorTexture"),
                            TextureSemantic::Color);
            texture_.pump();
            if (!texture_.error().empty()) {
                error_ = texture_.error();
                return;
            }
            if (!texture_.current())
                return;
            ready_ = true;
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    Diligent::ITextureView* render(unsigned width, unsigned height,
                                   std::array<float, 4> region = {0, 0, 1, 1}) {
        pump();
        if (!ready_)
            return nullptr;
        texture_.settings.region = region;
        texture_.settings.nearest = true;
        auto* image = texture_.render(width, height);
        if (!image || !texture_.error().empty()) {
            ready_ = false;
            error_ = texture_.error();
            return nullptr;
        }
        return image;
    }
    const TextureData* data() const { return ready_ ? texture_.data() : nullptr; }
    std::optional<ResourceIdentity> identity() const {
        return ready_ ? texture_.current_identity() : std::nullopt;
    }
    bool ready() const { return ready_; }
    const std::string& error() const { return error_; }
    std::uint64_t render_count() const { return texture_.render_count(); }

  private:
    std::filesystem::path project_;
    ResourcePool<MaterialAsset> materials_{{1, 4, 8, 2 * 1024 * 1024}};
    std::shared_ptr<const AssetCatalog> catalog_;
    AssetRef<MaterialAsset> material_;
    ResourceTicket ticket_;
    ResourceLease<MaterialAsset> ready_material_;
    std::optional<ResourceIdentity> attempted_material_;
    TextureAssetPreview texture_;
    bool ready_ = false;
    std::string error_;
};
} // namespace forge
