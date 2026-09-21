#pragma once
#include "../texture_asset_preview.hpp"
#include "asset_scene_preview.hpp"
#include "thumbnail_revision.hpp"
namespace forge {
// One shared scratch viewer per kind, one active request, no per-tile threads or
// render worlds. Completed images are disposable LRU data (at most 32 MiB).
class ContentThumbnails {
  public:
    struct View {
        Diligent::ITextureView* image = nullptr;
        std::string status;
    };
    ContentThumbnails(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                      std::filesystem::path project, std::shared_ptr<MeshResourceHost> host)
        : presentation_(presentation), context_(context), project_(std::move(project)),
          scene_(presentation, context, project_, std::move(host)),
          texture_(presentation, context, project_) {}
    ~ContentThumbnails() { stop_.request_stop(); }
    const std::filesystem::path& project() const { return project_; }
    static bool supports(const AssetRecord& record) {
        return record.type == "texture" || record.type == "model" || record.type == "mesh" ||
               record.type == "material";
    }
    void begin(std::shared_ptr<const AssetCatalog> catalog) {
        ++frame_;
        demanded_.clear();
        if (catalog != catalog_) {
            catalog_ = std::move(catalog);
            ++epoch_;
            stop_.request_stop();
        }
    }
    View request(AssetId id) {
        if (!catalog_ || !id)
            return {};
        const auto record = catalog_->records().find(id);
        if (record == catalog_->records().end() || !supports(record->second))
            return {};
        if (!record->second.metadata.contains("forge.import"))
            return {nullptr, "Import this asset to prepare a thumbnail."};
        if (record->second.subasset && record->second.subasset->removed)
            return {nullptr, "Removed member: no current thumbnail."};
        if (demanded_.size() == 256 && !demanded_.contains(id))
            return {nullptr, "Thumbnail visible-request limit reached."};
        demanded_.insert(id);
        auto found = entries_.find(id);
        if (found == entries_.end()) {
            if (entries_.size() == 128) {
                auto oldest = entries_.end();
                for (auto it = entries_.begin(); it != entries_.end(); ++it)
                    if (it->first != active_ && it->second.touched != frame_ &&
                        (oldest == entries_.end() || it->second.touched < oldest->second.touched))
                        oldest = it;
                if (oldest == entries_.end())
                    return {nullptr, "Thumbnail cache is full; offscreen tiles retire first."};
                retire(oldest->second.image);
                entries_.erase(oldest);
            }
            found = entries_.try_emplace(id).first;
        }
        auto& entry = found->second;
        entry.touched = frame_;
        return {entry.image ? entry.image->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)
                            : nullptr,
                !entry.error.empty()       ? "Thumbnail: " + entry.error
                : entry.verified != epoch_ ? (entry.image ? "Refreshing published thumbnail..."
                                                          : "Preparing published thumbnail...")
                                           : "Published asset thumbnail"};
    }
    void retry(AssetId id) {
        if (auto found = entries_.find(id); found != entries_.end()) {
            found->second.verified = 0;
            found->second.attempted.clear();
            found->second.error.clear();
        }
    }
    void advance() {
        if (revision_job_.valid()) {
            if (revision_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            try {
                const auto key = revision_job_.get();
                if (active_epoch_ != epoch_ || stop_.stop_requested() ||
                    !demanded_.contains(active_)) {
                    active_ = {};
                    return;
                }
                auto& entry = entries_.at(active_);
                if (entry.attempted == key) {
                    entry.verified = epoch_;
                    active_ = {};
                    return;
                }
                key_ = key;
                const auto& record = catalog_->records().at(active_);
                is_texture_ = record.type == "texture";
                if (is_texture_) {
                    texture_.select(catalog_, {active_});
                    if (!texture_.error().empty())
                        texture_.retry();
                } else {
                    scene_.select(catalog_, active_);
                    if (!scene_.error().empty())
                        scene_.retry();
                }
                loading_ = true;
            } catch (const std::exception& e) {
                fail(e.what());
                return;
            }
        }
        if (loading_) {
            if (active_epoch_ != epoch_ || !demanded_.contains(active_)) {
                if (is_texture_)
                    texture_.cancel();
                else
                    scene_.cancel();
                active_ = {};
                loading_ = false;
                return;
            }
            try {
                Diligent::ITextureView* source = nullptr;
                std::string error;
                bool pending;
                if (is_texture_) {
                    texture_.pump();
                    unsigned w = 256, h = 256;
                    if (const auto* data = texture_.data()) {
                        const double ratio = double(data->width) / data->height;
                        if (ratio > 1)
                            h = std::max(1u, unsigned(256 / ratio));
                        else
                            w = std::max(1u, unsigned(256 * ratio));
                    }
                    source = texture_.render(w, h);
                    pending = texture_.pending();
                    error = texture_.error();
                } else {
                    source = scene_.render(256, 256);
                    pending = scene_.pending();
                    error = scene_.error();
                }
                if (pending)
                    return;
                if (active_epoch_ != epoch_ || !demanded_.contains(active_)) {
                    active_ = {};
                    loading_ = false;
                    return;
                }
                if (!error.empty())
                    throw std::runtime_error(error);
                if (!source)
                    throw std::runtime_error("Preview completed without an image");
                auto desc = source->GetTexture()->GetDesc();
                desc.Name = "FORGE Content thumbnail";
                desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
                Diligent::RefCntAutoPtr<Diligent::ITexture> image;
                presentation_.device()->CreateTexture(desc, nullptr, &image);
                if (!image)
                    throw std::runtime_error("Thumbnail allocation failed");
                Diligent::CopyTextureAttribs copy;
                copy.pSrcTexture = source->GetTexture();
                copy.pDstTexture = image;
                copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode =
                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                context_->CopyTexture(copy);
                auto& entry = entries_.at(active_);
                retire(entry.image);
                entry.image = std::move(image);
                entry.attempted = key_;
                entry.verified = epoch_;
                entry.error.clear();
                ++completed_;
                active_ = {};
                loading_ = false;
            } catch (const std::exception& e) {
                fail(e.what());
            }
            return; // At most one rendered/copied thumbnail per UI frame.
        }
        for (const auto id : demanded_) {
            const auto found = entries_.find(id);
            if (found == entries_.end() || found->second.verified == epoch_)
                continue;
            active_ = id;
            active_epoch_ = epoch_;
            key_.clear();
            stop_ = std::stop_source{};
            try {
                revision_job_ = std::async(std::launch::async,
                                           [catalog = catalog_, id, stop = stop_.get_token()] {
                                               return thumbnail_revision(*catalog, id, stop);
                                           });
            } catch (const std::exception& e) {
                fail(e.what());
            }
            break;
        }
    }
    // Call after GUI submission. Native texture destruction uses Diligent's
    // backend deferred release, as for the central preview owners.
    void after_submission() { retired_.clear(); }
    std::size_t size() const { return entries_.size(); }
    std::uint64_t completed() const { return completed_; }
    std::size_t ready_count() const {
        return std::count_if(entries_.begin(), entries_.end(), [&](const auto& item) {
            const auto& entry = item.second;
            return catalog_ && catalog_->records().contains(item.first) && entry.image &&
                   entry.verified == epoch_ && entry.error.empty();
        });
    }

  private:
    struct Entry {
        Diligent::RefCntAutoPtr<Diligent::ITexture> image;
        std::string attempted, error;
        std::uint64_t touched = 0, verified = 0;
    };
    void retire(Diligent::RefCntAutoPtr<Diligent::ITexture>& image) {
        if (image)
            retired_.push_back(std::move(image));
    }
    void fail(const std::string& error) {
        if (active_epoch_ == epoch_)
            if (auto found = entries_.find(active_); found != entries_.end()) {
                found->second.error = error;
                found->second.attempted = key_;
                found->second.verified = epoch_;
            }
        active_ = {};
        loading_ = false;
    }
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::filesystem::path project_;
    AssetScenePreview scene_;
    TextureAssetPreview texture_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::map<AssetId, Entry> entries_;
    std::set<AssetId> demanded_;
    std::vector<Diligent::RefCntAutoPtr<Diligent::ITexture>> retired_;
    std::stop_source stop_;
    std::future<std::string> revision_job_;
    AssetId active_;
    std::string key_;
    std::uint64_t frame_ = 0, epoch_ = 0, active_epoch_ = 0, completed_ = 0;
    bool loading_ = false, is_texture_ = false;
};
} // namespace forge
