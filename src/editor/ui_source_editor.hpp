#pragma once
#include "../project_text_source.hpp"
#include "../ui_markup_authoring.hpp"
#include "automation_schema.hpp"
#include "document.hpp"
#include "document_workspace.hpp"
#include "editor_state.hpp"
#include "source_text_widget.hpp"
#include "widgets.hpp"
#include <RmlUi/Core/StringUtilities.h>
#include <array>
#include <cstdio>
#include <utility>

namespace forge::ui {
// One native document and widget-owned history. Outline/properties are derived.
class UiSourceEditor {
    TextEditor editor_;
    std::filesystem::path root_, locator_;
    std::optional<std::filesystem::path> queued_;
    std::string disk_, saved_, session_, error_, outline_error_;
    std::vector<UiMarkupElement> outline_;
    std::size_t selected_ = UiMarkupElement::no_parent;
    std::uint64_t revision_ = 0;
    bool dirty_ = false, close_ = false, focus_ = false, text_editable_ = false;
    char attribute_[65] = "id", value_[8193]{}, literal_[8193]{};
    int child_kind_ = 0;

    void changed() {
        const auto text = editor_.GetText();
        dirty_ = text != saved_;
        ++revision_;
        selected_ = UiMarkupElement::no_parent;
        outline_.clear();
        outline_error_.clear();
        if (locator_.extension() == ".rml") {
            try {
                outline_ = inspect_ui_markup(text);
            } catch (const std::exception& error) {
                outline_error_ = error.what();
            }
        }
    }
    void check_project(SceneDocument& project) const {
        project.check_ownership();
        if (!is_open() || root_ != project.project())
            throw AuthoringError("source_scope", "Open a UI source in the current project first");
    }
    void finish_close() {
        root_.clear();
        locator_.clear();
        disk_.clear();
        saved_.clear();
        session_.clear();
        outline_.clear();
        error_.clear();
        outline_error_.clear();
        selected_ = UiMarkupElement::no_parent;
        dirty_ = close_ = false;
        editor_.SetText("");
    }
    template <class F> void attempt(F&& operation) {
        try {
            operation();
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
            report_error("ui_source_editor.hpp", error_);
        }
    }
    void select(std::size_t index) {
        selected_ = index;
        const auto text = editor_.GetText();
        const auto& node = outline_.at(index);
        std::string value;
        for (const auto& attr : node.attributes)
            if (attr.name == attribute_ && attr.has_value)
                value = Rml::StringUtilities::DecodeRml(
                    text.substr(attr.value.begin, attr.value.end - attr.value.begin));
        std::snprintf(value_, sizeof(value_), "%s", value.c_str());
        auto content = text.substr(node.content.begin, node.content.end - node.content.begin);
        text_editable_ = ui_markup_widget_editable(node) && !node.self_closing &&
                         content.find('<') == content.npos;
        if (text_editable_)
            content = Rml::StringUtilities::DecodeRml(content);
        text_editable_ = text_editable_ && content.size() < sizeof(literal_);
        std::snprintf(literal_, sizeof(literal_), "%s", text_editable_ ? content.c_str() : "");
    }
    void element_controls(bool locked) {
        if (locator_.extension() != ".rml") {
            ImGui::TextWrapped("Edit stylesheet rules in the Source tab.");
            return;
        }
        if (!outline_error_.empty()) {
            ImGui::TextWrapped("Complete the markup in Source to use element controls. %s",
                               outline_error_.c_str());
            return;
        }
        const auto snapshot = editor_.GetText();
        const auto draw_node = [&](auto&& self, std::size_t& cursor) -> void {
            const auto index = cursor++;
            const auto& node = outline_[index];
            const bool leaf = cursor == outline_.size() || outline_[cursor].parent != index;
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (node.tag == "body")
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            if (leaf)
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (selected_ == index)
                flags |= ImGuiTreeNodeFlags_Selected;
            auto label = "<" + node.tag + ">";
            for (const auto& attr : node.attributes)
                if (attr.name == "id" && attr.has_value) {
                    label += " #" + Rml::StringUtilities::DecodeRml(snapshot.substr(
                                        attr.value.begin, attr.value.end - attr.value.begin));
                    break;
                }
            const bool open =
                ImGui::TreeNodeEx(reinterpret_cast<void*>(index + 1), flags, "%s", label.c_str());
            FORGE_UI_PROBE("ui-source:element:" + std::to_string(index));
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                select(index);
            if (open) {
                while (cursor < outline_.size() && outline_[cursor].source.begin < node.source.end)
                    self(self, cursor);
                ImGui::TreePop();
            } else
                while (cursor < outline_.size() && outline_[cursor].source.begin < node.source.end)
                    ++cursor;
        };
        for (std::size_t i = 0; i < outline_.size(); ++i)
            if (outline_[i].tag == "body") {
                auto cursor = i;
                draw_node(draw_node, cursor);
                i = cursor - 1;
            }
        if (selected_ >= outline_.size()) {
            ImGui::TextWrapped("Select a container or widget to edit its source.");
            return;
        }
        // Snapshot affordances before an edit can rebuild the derived outline.
        const auto index = selected_;
        const auto& node = outline_[index];
        const auto tag = node.tag;
        const bool widget_editable = ui_markup_widget_editable(node);
        const bool container = !node.self_closing && (node.tag == "body" || widget_editable);
        bool previous = false, next = false;
        for (std::size_t i = 0; i < outline_.size(); ++i)
            if (outline_[i].parent == node.parent) {
                previous = previous || i < index;
                next = next || i > index;
            }
        ImGui::Separator();
        ImGui::Text("Selected: <%s>", tag.c_str());
        ImGui::BeginDisabled(locked);
        ImGui::InputText("Attribute", attribute_, sizeof(attribute_));
        FORGE_UI_PROBE("ui-source:attribute-name");
        ImGui::InputText("Value", value_, sizeof(value_));
        FORGE_UI_PROBE("ui-source:attribute-value");
        if (button(
                "Set attribute",
                "Escape markup characters in the value; RML bindings keep their source meaning."))
            attempt([&] { attribute(index, attribute_, std::string(value_)); });
        FORGE_UI_PROBE("ui-source:attribute-set");
        ImGui::SameLine();
        if (button("Remove attribute", "Remove only this attribute from the selected element."))
            attempt([&] { attribute(index, attribute_, {}); });
        ImGui::BeginDisabled(!text_editable_);
        ImGui::InputTextMultiline("Text", literal_, sizeof(literal_), {0, 70});
        FORGE_UI_PROBE("ui-source:literal");
        if (button(
                "Set text",
                "Escape markup characters in leaf text; RML bindings keep their source meaning."))
            attempt([&] { text(index, literal_); });
        FORGE_UI_PROBE("ui-source:text-set");
        ImGui::EndDisabled();
        const char* kinds[] = {"div", "span", "p", "button", "input", "img", "br"};
        ImGui::BeginDisabled(!container);
        ImGui::Combo("New child", &child_kind_, kinds, IM_ARRAYSIZE(kinds));
        if (button("Add child", "Append a new element inside the selected container."))
            attempt([&] { insert(index, kinds[child_kind_], {}); });
        FORGE_UI_PROBE("ui-source:child-add");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!widget_editable || !previous);
        if (button("Move up", "Swap with the previous sibling; surrounding source stays intact."))
            attempt([&] { move(index, -1); });
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!widget_editable || !next);
        if (button("Move down", "Swap with the next sibling."))
            attempt([&] { move(index, 1); });
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!widget_editable);
        if (button("Remove element",
                   "Remove the selected widget and its child source as one Undo step."))
            attempt([&] { remove(index); });
        FORGE_UI_PROBE("ui-source:element-remove");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }

  public:
    bool close_cancelled = false;
    std::function<bool()> save_shortcut = [] {
        return ImGui::GetIO().KeyMods == ImGuiMod_Ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false);
    };
    UiSourceEditor() {
        editor_.SetColorizerEnable(false);
        editor_.SetShowWhitespaces(false);
        editor_.SetTabSize(4);
    }
    bool is_open() const { return !locator_.empty(); }
    bool dirty() const { return dirty_; }
    bool close_pending() const { return close_; }
    const std::filesystem::path& project() const { return root_; }
    bool owns_keyboard() const { return is_open() && source_widget_owns_keyboard("###UI Source"); }
    bool can_undo() const { return is_open() && editor_.CanUndo(); }
    bool can_redo() const { return is_open() && editor_.CanRedo(); }
    WorkspaceDocument workspace_document(SceneDocument& project, EditorUiContext& context,
                                         std::function<bool()> locked) {
        return {"ui_source",
                "UI Source",
                "UI Source###UI Source",
                true,
                [this] { return is_open(); },
                [this] { return dirty(); },
                [this, &project, &context, locked] { draw(project, context, locked && locked()); },
                [this, &project] { save(project); },
                [this] { undo(false); },
                [this] { undo(true); },
                [this] { request_close(); },
                [this] { return can_undo(); },
                [this] { return can_redo(); },
                {},
                [this] { return std::exchange(close_cancelled, false); }};
    }
    bool owns(const std::filesystem::path& root, const std::filesystem::path& path) const {
        return is_open() && root == root_ && ProjectPaths(root).same_locator(locator_, path);
    }
    Json state() const {
        if (!is_open())
            return {{"open", false}};
        const auto text = editor_.GetText();
        return {{"open", true},
                {"path", path_utf8(locator_)},
                {"session", session_},
                {"revision", revision_},
                {"state", ProjectTextSource::digest(text)},
                {"disk_state", ProjectTextSource::digest(disk_)},
                {"dirty", dirty_},
                {"can_undo", can_undo()},
                {"can_redo", can_redo()},
                {"text", text},
                {"outline_error", outline_error_}};
    }
    const std::vector<UiMarkupElement>& outline() const { return outline_; }
    void open(SceneDocument& project, const std::string& locator) {
        project.check_ownership();
        const auto path = automation_relative_path(locator);
        if (path.extension() != ".rml" && path.extension() != ".rcss")
            throw AuthoringError("unsupported_source", "Choose an RML document or RCSS stylesheet");
        if (is_open() && root_ != project.project()) {
            if (dirty_)
                throw AuthoringError("source_scope", "Close the previous project's UI draft first");
            finish_close();
        }
        if (owns(project.project(), path)) {
            focus_ = true;
            return;
        }
        if (dirty_) {
            queued_ = path;
            close_ = true;
            return;
        }
        const auto file = ProjectTextSource::path(project.project(), path);
        const auto disk = ProjectTextSource::read(file);
        if (!disk)
            throw AuthoringError("source_missing", "This UI source no longer exists");
        if (disk->size() >= 256 * 1024)
            throw AuthoringError("source_limit",
                                 "UI source must leave room for a final newline within256KiB");
        root_ = project.project();
        locator_ = path;
        disk_ = *disk;
        editor_.SetText(*disk);
        saved_ = editor_.GetText();
        session_ = AssetId::generate().str();
        revision_ = 0;
        focus_ = true;
        close_ = false;
        changed();
    }
    void replace_text(const std::string& value) {
        if (!is_open())
            throw AuthoringError("source_closed", "Open a UI source first");
        ProjectTextSource::validate_text(value);
        if (value.size() >= 256 * 1024)
            throw AuthoringError("source_limit",
                                 "UI source must fit within256KiB including its final newline");
        if (editor_.GetText() == value)
            return;
        replace_source_widget_text(editor_, value);
        changed();
    }
    void attribute(std::size_t index, std::string_view name, std::optional<std::string> value) {
        replace_text(set_ui_markup_attribute(editor_.GetText(), index, name, std::move(value)));
    }
    void text(std::size_t index, std::string_view literal) {
        replace_text(set_ui_markup_text(editor_.GetText(), index, literal));
    }
    void insert(std::size_t parent, std::string_view tag, std::string_view literal,
                std::optional<std::size_t> before = {}) {
        replace_text(insert_ui_markup_child(editor_.GetText(), parent, tag, literal, before));
    }
    void remove(std::size_t index) {
        replace_text(remove_ui_markup_element(editor_.GetText(), index));
    }
    void move(std::size_t index, int direction) {
        replace_text(move_ui_markup_element(editor_.GetText(), index, direction));
    }
    void undo(bool redo) {
        if (redo ? !can_redo() : !can_undo())
            return;
        if (redo)
            editor_.Redo();
        else
            editor_.Undo();
        changed();
    }
    void save(SceneDocument& project) {
        check_project(project);
        if (!dirty_)
            return;
        const auto file = ProjectTextSource::path(root_, locator_);
        const auto value = editor_.GetText();
        auto stored = value;
        bool crlf = disk_.find("\r\n") != disk_.npos;
        for (std::size_t i = 0; i < disk_.size() && crlf; ++i)
            if (disk_[i] == '\n' && (!i || disk_[i - 1] != '\r'))
                crlf = false;
        if (crlf) {
            stored.clear();
            for (const auto c : value) {
                if (c == '\n')
                    stored += '\r';
                stored += c;
            }
        }
        ProjectTextSource::validate(file, stored);
        ProjectTextSource::write_validated(file, disk_, stored);
        disk_ = std::move(stored);
        saved_ = value;
        dirty_ = false;
    }
    void request_close() {
        queued_.reset();
        if (dirty_)
            close_ = true;
        else
            finish_close();
    }
    void discard() { finish_close(); }
    void source_file_changed(const std::filesystem::path& previous,
                             const std::optional<std::filesystem::path>& destination) {
        if (!is_open() || !ProjectPaths(root_).same_locator(locator_, previous))
            return;
        if (dirty_ || close_)
            throw AuthoringError("source_busy",
                                 "Settle the open UI draft before moving its source");
        if (!destination || !ProjectTextSource::allowed_root(*destination) ||
            (destination->extension() != ".rml" && destination->extension() != ".rcss"))
            finish_close();
        else {
            (void)ProjectTextSource::path(root_, *destination);
            locator_ = *destination;
            changed();
        }
    }
    void reload(SceneDocument& project, bool discard_changes = false) {
        check_project(project);
        if (dirty_ && !discard_changes)
            throw AuthoringError("unsaved_source", "Save or explicitly discard this UI draft");
        const auto path = path_utf8(locator_);
        finish_close();
        open(project, path);
    }
    void draw(SceneDocument& project, EditorUiContext& context, bool locked) {
        if (!is_open())
            return;
        locked = locked || root_ != project.project();
        editor_.SetReadOnly(locked);
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        bool visible = true;
        const bool contents = ImGui::Begin("UI Source###UI Source", &visible);
        const bool save_key = !locked && save_shortcut && save_shortcut() &&
                              ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            context.task.focus_document("ui_source", "UI Source");
        if (!visible)
            request_close();
        if (contents && is_open()) {
            ImGui::TextWrapped("%s%s", path_utf8(locator_).c_str(), dirty_ ? " *" : "");
            ImGui::BeginDisabled(locked);
            if (button("Save source", "Save this source at its disk baseline; registration and "
                                      "Reload UI stay explicit."))
                attempt([&] { save(project); });
            FORGE_UI_PROBE("ui-source:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (button("Undo", "Undo within this UI source document."))
                undo(false);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (button("Redo", "Redo within this UI source document."))
                undo(true);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (button("Reload from disk",
                       "Reload this source; a dirty draft asks before discarding."))
                attempt([&] {
                    if (dirty_) {
                        queued_ = locator_;
                        close_ = true;
                    } else
                        reload(project);
                });
            ImGui::EndDisabled();
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            if (ImGui::BeginTabBar("ui-source-view")) {
                const bool source_tab = ImGui::BeginTabItem("Source");
                FORGE_UI_PROBE("ui-source:source-tab");
                if (source_tab) {
                    editor_.Render("ui-source-text", ImGui::GetContentRegionAvail());
                    if (editor_.IsTextChanged())
                        changed();
                    ImGui::EndTabItem();
                }
                const bool elements_tab = ImGui::BeginTabItem("Elements");
                FORGE_UI_PROBE("ui-source:elements-tab");
                if (elements_tab) {
                    element_controls(locked);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            if (save_key)
                attempt([&] { save(project); });
        }
        if (close_)
            ImGui::OpenPopup("Save UI source?");
        if (ImGui::BeginPopupModal("Save UI source?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save this UI source, discard its draft, or keep editing?");
            ImGui::BeginDisabled(locked);
            if (button("Save", "Save before closing."))
                attempt([&] {
                    save(project);
                    finish_close();
                    ImGui::CloseCurrentPopup();
                });
            ImGui::SameLine();
            if (button("Discard", "Close this draft without writing.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (button("Keep editing", "Cancel the close or source switch.")) {
                close_ = false;
                queued_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::End();
        if (!is_open() && queued_) {
            const auto next = std::exchange(queued_, {});
            attempt([&] { open(project, path_utf8(*next)); });
        }
    }
};
} // namespace forge::ui
