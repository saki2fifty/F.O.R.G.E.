#pragma once
#include "editor_state.hpp"
#include <stdexcept>
namespace forge::ui {
// Private UI adapters borrow document owners. They never store authored data or persistent IDs.
struct WorkspaceDocument {
    std::string id, title, window_id;
    bool central = true;
    std::function<bool()> is_open, dirty;
    std::function<void()> draw, save, undo, redo, request_close;
    std::function<bool()> can_undo, can_redo;
    std::function<void(const std::string&)> inspect;
    std::function<bool()> take_close_cancelled;
};
class DocumentWorkspace {
  public:
    void add(WorkspaceDocument document) {
        if (document.id.empty() || find(document.id))
            throw std::logic_error("Duplicate/empty editor document adapter");
        entries_.push_back(std::move(document));
    }
    void remove(const std::string& id, EditorUiContext* context = nullptr) {
        if (context) {
            if (context->selection.kind() == SelectionKind::DocumentItem &&
                context->selection.document() == id)
                context->selection.clear();
            if (context->task.id() == id)
                context->task.owner = DocumentTask::Scene;
        }
        std::erase_if(entries_, [&](const auto& d) { return d.id == id; });
    }
    const WorkspaceDocument* find(const std::string& id) const {
        for (const auto& d : entries_)
            if (d.id == id)
                return &d;
        return nullptr;
    }
    bool available(const std::string& id) const {
        auto* d = find(id);
        return d && (!d->is_open || d->is_open());
    }
    bool history(const std::string& id, bool redo) const {
        const auto* d = find(id);
        if (!available(id))
            return false;
        const auto& can = redo ? d->can_redo : d->can_undo;
        return can && can();
    }
    bool save(const std::string& id) const {
        const auto* d = find(id);
        if (!available(id) || !d->save)
            return false;
        d->save();
        return true;
    }
    bool undo(const std::string& id, bool redo) const {
        const auto* d = find(id);
        if (!history(id, redo))
            return false;
        const auto& run = redo ? d->redo : d->undo;
        if (!run)
            return false;
        run();
        return true;
    }
    bool inspect(const std::string& id, const std::string& local_key) const {
        const auto* d = find(id);
        if (!available(id) || !d->inspect)
            return false;
        d->inspect(local_key);
        return true;
    }
    void draw() const {
        for (const auto& d : entries_)
            if (d.draw)
                d.draw();
    }
    bool source_drafts_dirty() const {
        for (const auto& d : entries_)
            if (d.id != "scene" && d.dirty && d.dirty())
                return true;
        return false;
    }
    // Asset/source drafts settle before the scene's existing file guard. Each
    // owner retains its own Save/history/publication semantics and close dialog.
    bool close_pending_sources() const {
        for (const auto& d : entries_)
            if (d.id != "scene" && d.dirty && d.dirty()) {
                if (d.request_close)
                    d.request_close();
                return false;
            }
        return true;
    }
    bool consume_close_cancellation() const {
        bool cancelled = false;
        for (const auto& d : entries_)
            if (d.take_close_cancelled)
                cancelled = d.take_close_cancelled() || cancelled;
        return cancelled;
    }
    void dock(ImGuiID center) const {
        for (const auto& d : entries_)
            if (d.central && !d.window_id.empty())
                ImGui::DockBuilderDockWindow(d.window_id.c_str(), center);
    }

  private:
    std::vector<WorkspaceDocument> entries_;
};
class AssetEditors {
  public:
    struct Entry {
        std::string type, label;
        std::function<void(const AssetRecord&)> open;
    };
    void add(Entry entry) {
        if (find(entry.type))
            throw std::logic_error("Duplicate asset editor");
        entries_.push_back(std::move(entry));
    }
    const Entry* find(const std::string& type) const {
        for (const auto& e : entries_)
            if (e.type == type)
                return &e;
        return nullptr;
    }
    bool open(const AssetRecord& asset) const {
        const auto* e = find(asset.type);
        if (!e)
            return false;
        e->open(asset);
        return true;
    }

  private:
    std::vector<Entry> entries_;
};
} // namespace forge::ui
