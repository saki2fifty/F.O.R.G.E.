#pragma once
#include "../asset_import_service.hpp"
#include "../asset_source_document.hpp"
#include "document.hpp"
#include <utility>

namespace forge {
struct AssetDocumentAutomation;
// Shared window lifecycle only. AssetSourceDocument owns values/history and the
// import service owns publication; concrete views retain their own gestures.
template <class Traits> class SourceDocumentPanel {
    friend struct AssetDocumentAutomation;

  public:
    using Document = AssetSourceDocument<Traits>;
    using Registry = std::function<std::shared_ptr<const AssetImporterRegistry>()>;
    using Prepare = std::function<void(AssetPublicationCandidate&, const AssetImportPlan&)>;
    SourceDocumentPanel(std::string label, Registry registry, Prepare prepare, ImportTarget target)
        : label_(std::move(label)), registry_(std::move(registry)), prepare_(std::move(prepare)),
          target_(std::move(target)) {}
    virtual ~SourceDocumentPanel() = default;
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const {
        return document_ && (document_->dirty() || gesture_dirty() || needs_publish_ || pending());
    }
    bool can_undo() const {
        return document_ && !pending() && (gesture_dirty() || document_->can_undo());
    }
    bool can_redo() const {
        return document_ && !pending() && !gesture_dirty() && document_->can_redo();
    }
    const Document* document() const { return document_.get(); }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() { save_ = true; }
    void request_close() {
        if (document_)
            (void)flush_gesture();
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (gesture_dirty()) {
            cancel_gesture();
            return;
        }
        if (can_undo()) {
            document_->undo();
            changed();
        }
    }
    void redo() {
        if (can_redo()) {
            document_->redo();
            changed();
        }
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            next_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& error) {
            error_ = error.what();
            open_error_ = true;
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
            finish_close();
            next_source_.reset();
        }
        if (service_)
            for (auto& result : service_->poll()) {
                if (result.job.id != job_)
                    continue;
                job_ = 0;
                if (result.published) {
                    published_ = std::make_shared<const AssetCatalog>(result.publication->catalog);
                    catalog_ = published_;
                    catalog_changed();
                    needs_publish_ = false;
                    error_.clear();
                    message = label_ + " saved and published.";
                } else {
                    needs_publish_ = true;
                    close_ = false;
                    error_ = result.diagnostic;
                }
            }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_source_) {
            auto source = std::exchange(next_source_, {});
            open(project, *source);
        }
    }

  protected:
    std::unique_ptr<Document> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> next_source_;
    AssetJobId job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    std::string error_;
    virtual bool gesture_dirty() const { return false; }
    virtual void cancel_gesture() {}
    virtual bool flush_gesture() { return true; }
    virtual void view_closed() {}
    virtual void source_opened() {}
    virtual void catalog_changed() {}
    virtual void changed() { needs_publish_ = true; }
    bool edit_source(std::uint64_t revision, std::string label,
                     const std::function<void(Json&)>& mutation) {
        try {
            if (!document_ || pending())
                return false;
            document_->edit(revision, std::move(label), mutation);
            changed();
            error_.clear();
            return true;
        } catch (const std::exception& error) {
            error_ = error.what();
            return false;
        }
    }
    void finish_close() {
        cancel_gesture();
        view_closed();
        document_.reset();
        service_.reset();
        catalog_.reset();
        job_ = 0;
        needs_publish_ = save_ = close_ = open_error_ = false;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto doc = std::make_unique<Document>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service =
            std::make_unique<AssetImportService>(project.writer_guard(), registry_(), target_);
        const auto at = catalog->records().find(doc->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    Traits::byte_limit);
        const bool needs = at == catalog->records().end() ||
                           !at->second.metadata.contains("forge.import") ||
                           at->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(doc);
        service_ = std::move(service);
        catalog_ = std::move(catalog);
        needs_publish_ = needs;
        source_opened();
        error_.clear();
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [prepare = prepare_](auto& candidate, const auto& plan, const auto&) {
                    prepare(candidate, plan);
                },
                [](const auto&, const auto&) {});
        } catch (const std::exception& error) {
            needs_publish_ = true;
            close_ = false;
            error_ = error.what();
        }
    }

  private:
    std::string label_;
    Registry registry_;
    Prepare prepare_;
    ImportTarget target_;
};
} // namespace forge
