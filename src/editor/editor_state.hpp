#pragma once
#include "widgets.hpp"
#include <deque>
#include <forge/assets.hpp>
#include <forge/scene.hpp>
#include <functional>
#include <set>
namespace forge::ui {
// Editor-only identity/scope. World/component/asset contents remain in their existing owners.
enum class SelectionKind { None, Entity, Asset, PrefabMember, DocumentItem };
class EditorSelection {
  public:
    SelectionKind kind() const { return entity_.empty() ? kind_ : SelectionKind::Entity; }
    const std::string& entity() const { return entity_; }
    std::string& entity_slot() { return entity_; }
    AssetId asset() const { return asset_; }
    const std::string& member() const { return member_; }
    void clear() {
        kind_ = SelectionKind::None;
        entity_.clear();
        asset_ = {};
        member_.clear();
        document_.clear();
    }
    void select_entity(std::string id) {
        clear();
        entity_ = std::move(id);
    }
    void select_asset(AssetId id) {
        clear();
        asset_ = id;
        kind_ = SelectionKind::Asset;
    }
    void select_member(AssetId asset, std::string member) {
        select_asset(asset);
        member_ = std::move(member);
        kind_ = SelectionKind::PrefabMember;
    }
    const std::string& document() const { return document_; }
    void select_document_item(std::string document, std::string local_key) {
        clear();
        kind_ = SelectionKind::DocumentItem;
        document_ = std::move(document);
        member_ = std::move(local_key);
    }
    void reconcile(const Json& document) {
        if (!entity_.empty()) {
            kind_ = SelectionKind::None;
            asset_ = {};
            member_.clear();
            bool found = false;
            for (const auto& row : document.at("entities"))
                found |= row.at("id") == entity_;
            if (!found)
                clear();
        }
    }

  private:
    SelectionKind kind_ = SelectionKind::None;
    std::string entity_, member_, document_;
    AssetId asset_;
};
enum class DocumentTask { Scene, Prefab, Settings, Extension };
enum class DraftResolution { Save, Discard, Cancel };
struct ActiveTask {
    DocumentTask owner = DocumentTask::Scene;
    std::string extension_id, extension_title;
    std::string id() const {
        return owner == DocumentTask::Prefab      ? "prefab"
               : owner == DocumentTask::Settings  ? "settings"
               : owner == DocumentTask::Extension ? extension_id
                                                  : "scene";
    }
    void focus_document(std::string id, std::string title) {
        owner = DocumentTask::Extension;
        extension_id = std::move(id);
        extension_title = std::move(title);
    }

    void focus(DocumentTask task) {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            owner = task;
    }
    const char* name() const {
        if (owner == DocumentTask::Extension)
            return extension_title.c_str();
        return owner == DocumentTask::Prefab     ? "Prefab source"
               : owner == DocumentTask::Settings ? "Project Settings"
                                                 : "Scene";
    }
};
// Shared context is explicitly installed by the one editor UI owner (also usable by UI fixtures).
struct Problem {
    std::string key, severity, text, entity, source, property;
    AssetId asset;
    int line = 0, column = 0;
    bool source_navigation = false;
};
class Problems {
  public:
    void report(Problem value) {
        if (value.text.empty())
            return;
        for (auto& p : items_)
            if (p.key == value.key) {
                p = std::move(value);
                return;
            }
        if (items_.size() == 256)
            items_.pop_front();
        items_.push_back(std::move(value));
    }
    void ingest(Problem value) {
        if (!seen_.insert(value.key).second)
            return;
        if (seen_.size() > 2048)
            seen_.erase(seen_.begin());
        report(std::move(value));
    }
    void reset() {
        items_.clear();
        seen_.clear();
    }
    void resolve(const std::string& key) {
        std::erase_if(items_, [&](const auto& p) { return p.key == key; });
    }
    void clear() { items_.clear(); }
    const auto& items() const { return items_; }
    std::size_t size() const { return items_.size(); }
    bool draw(EditorSelection& selection, bool* open,
              const std::function<void(const Problem&)>& open_source = {}) {
        bool navigated = false;
        const auto title = "Problems (" + std::to_string(size()) + ")###Problems";
        if (ImGui::Begin(title.c_str(), open)) {
            heading("Needs attention",
                    "Errors and warnings retained for this editor session. Select an entry to "
                    "inspect its target. Console keeps details.");
            if (button("Clear resolved / dismiss", "Dismiss this session's displayed problems; "
                                                   "this does not change project data."))
                clear();
            if (items_.empty())
                ImGui::TextUnformatted("No problems reported.");
            for (const auto& p : items_) {
                ImGui::PushID(p.key.c_str());
                const auto label = p.severity + ": " + p.text;
                const float wrap = std::max(40.f, ImGui::GetContentRegionAvail().x);
                const auto size = ImGui::CalcTextSize(label.c_str(), nullptr, false, wrap);
                const auto position = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##problem", false, 0, {0, size.y})) {
                    if (!p.entity.empty()) {
                        selection.select_entity(p.entity);
                        navigated = true;
                    } else if (p.asset) {
                        selection.select_asset(p.asset);
                        navigated = true;
                    }
                }
                ImGui::GetWindowDrawList()->AddText(
                    ImGui::GetFont(), ImGui::GetFontSize(), position,
                    (p.severity == "warning" || p.severity == "Warning")
                        ? IM_COL32(235, 197, 112, 255)
                        : IM_COL32(250, 164, 145, 255),
                    label.c_str(), nullptr, wrap);
                help("Select the related entity or asset when available. Dismissing a message does "
                     "not fix its cause.");
                if (!p.property.empty())
                    ImGui::TextWrapped("Property: %s", p.property.c_str());
                if (!p.source.empty())
                    ImGui::TextWrapped("Source: %s", p.source.c_str());
                if (p.source_navigation && open_source &&
                    button(
                        "Open source",
                        "Show the source of this diagnostic without discarding an unsaved draft."))
                    open_source(p);
                ImGui::PopID();
            }
        }
        ImGui::End();
        return navigated;
    }

  private:
    std::deque<Problem> items_;
    std::set<std::string> seen_;
};
struct EditorUiContext {
    Scene* scene = nullptr;
    EditorSelection selection;
    ActiveTask task;
    Problems problems;
    std::deque<std::string> log;
    std::map<std::string, std::string> last_status;
    void record_status(const std::string& source, const std::string& text) {
        if (text.empty() || last_status[source] == text)
            return;
        last_status[source] = text;
        if (log.size() == 256)
            log.pop_front();
        log.push_back(source + ": " + text);
    }
    bool reveal_content = false, add_component = false, rename_entity = false;
};
inline EditorUiContext* editor_context = nullptr;
struct ContextScope {
    EditorUiContext* previous = editor_context;
    explicit ContextScope(EditorUiContext& c) { editor_context = &c; }
    ~ContextScope() { editor_context = previous; }
};
inline void report_error(const std::string& key, const std::string& text) {
    if (editor_context)
        editor_context->problems.report({key, "Error", text, {}, {}, {}, {}});
}
inline void field_error(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.f, .62f, .52f, 1});
    ImGui::TextWrapped("Error: %s", text.c_str());
    ImGui::PopStyleColor();
    help("The previous valid value is retained. Correct the value and retry.");
}
struct DisabledScope {
    explicit DisabledScope(bool disabled) { ImGui::BeginDisabled(disabled); }
    ~DisabledScope() { ImGui::EndDisabled(); }
    DisabledScope(const DisabledScope&) = delete;
    DisabledScope& operator=(const DisabledScope&) = delete;
};
struct PopupScope {
    ~PopupScope() { ImGui::EndPopup(); }
};
struct IdScope {
    explicit IdScope(const char* id) { ImGui::PushID(id); }
    ~IdScope() { ImGui::PopID(); }
};
} // namespace forge::ui
