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
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

namespace forge::ui {
struct UiSourcePreviewFrame {
    Json state;
    ImTextureID image = 0;
};
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
    UiSourcePreviewFrame preview_frame_;
    int preview_width_ = 1000, preview_height_ = 700;
    float preview_density_ = 1;
    std::array<float, 4> layout_controls_{};
    std::string layout_controls_state_;
    std::size_t layout_controls_element_ = UiMarkupElement::no_parent;
    struct LayoutGesture {
        std::string source, session;
        std::uint64_t revision;
        std::size_t element;
        UiPixelLayout initial, pixels;
        ImVec2 pointer;
        float scale = 1;
        bool resize = false, valid = false;
        std::string candidate;
        UiSourcePreviewFrame frame;
    };
    std::optional<LayoutGesture> gesture_;

    void changed() {
        gesture_.reset();
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
        gesture_.reset();
        preview_frame_ = {};
        if (close_preview)
            close_preview();
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
    static std::string source_id(std::string_view text, const UiMarkupElement& node) {
        std::string id;
        unsigned count = 0;
        for (const auto& attr : node.attributes)
            if (attr.name == "id") {
                ++count;
                if (attr.has_value)
                    id = Rml::StringUtilities::DecodeRml(std::string(
                        text.substr(attr.value.begin, attr.value.end - attr.value.begin)));
            }
        return count == 1 ? id : std::string{};
    }
    bool preview_current() const {
        return is_open() && preview_frame_.state.is_object() &&
               !preview_frame_.state.value("stale", true) &&
               preview_frame_.state.value("source_state", "") ==
                   ProjectTextSource::digest(editor_.GetText());
    }
    UiSourcePreviewFrame request_preview(const std::string& text,
                                         std::span<const UiMarkupElement> nodes, bool refresh) {
        if (!preview || locator_.extension() != ".rml")
            throw AuthoringError("preview_unavailable",
                                 "Open a registered RML document to preview it");
        auto frame =
            preview(locator_, text, preview_width_, preview_height_, preview_density_, refresh);
        if (!frame.state.is_object())
            throw AuthoringError("preview_unavailable", "Native UI preview is unavailable");
        const bool current =
            !frame.state.value("stale", true) &&
            frame.state.value("source_state", "") == ProjectTextSource::digest(text);
        frame.state["stale"] = !current;
        std::map<std::string, std::vector<std::size_t>> ids;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            auto id = source_id(text, nodes[i]);
            if (!id.empty())
                ids[std::move(id)].push_back(i);
        }
        if (frame.state.contains("elements"))
            for (auto& element : frame.state.at("elements")) {
                element["source_index"] = nullptr;
                auto reason = element.value("reason", "");
                const auto found = ids.find(element.at("id").get<std::string>());
                if (!current)
                    reason = "Preview is stale; refresh the current draft before editing";
                else if (found == ids.end() || found->second.size() != 1)
                    reason = "Canvas editing requires one matching authored element ID";
                else {
                    const auto index = found->second.front();
                    element["source_index"] = index;
                    if (reason.empty()) {
                        try {
                            const auto source = read_ui_markup_layout(text, nodes, index);
                            const auto& native = element.at("pixels");
                            const std::array values{source.left, source.top, source.width,
                                                    source.height};
                            constexpr std::array names{"left", "top", "width", "height"};
                            for (std::size_t k = 0; k < names.size(); ++k)
                                if (std::abs(native.at(names[k]).get<float>() - values[k]) > .001f)
                                    throw std::runtime_error("Inline layout differs from the "
                                                             "native cascade; use Source");
                        } catch (const std::exception& error) {
                            reason = error.what();
                        }
                    }
                }
                element["reason"] = reason;
                element["editable"] = reason.empty();
            }
        return frame;
    }
    const Json* preview_element(std::size_t index) const {
        if (preview_frame_.state.contains("elements"))
            for (const auto& element : preview_frame_.state.at("elements"))
                if (element.at("source_index") == index)
                    return &element;
        return nullptr;
    }
    void preview_controls(SceneDocument& project, bool locked) {
        if (locked) {
            gesture_.reset();
            ImGui::TextWrapped("Stop Play and finish the owning task before previewing UI source.");
            return;
        }
        if (locator_.extension() != ".rml") {
            ImGui::TextWrapped(
                "Open an RML document to preview it. Linked RCSS uses its saved bytes.");
            return;
        }
        ImGui::BeginDisabled(gesture_active());
        int dimensions[]{preview_width_, preview_height_};
        ImGui::SetNextItemWidth(std::max(120.f, ImGui::GetContentRegionAvail().x - 90));
        ImGui::InputInt2("Size (px)", dimensions);
        preview_width_ = dimensions[0];
        preview_height_ = dimensions[1];
        ImGui::SetNextItemWidth(80);
        ImGui::InputFloat("Density", &preview_density_, 0, 0, "%.2f");
        ImGui::SameLine();
        const bool refresh =
            button("Refresh preview", "Reload the draft and its saved dependencies.");
        FORGE_UI_PROBE("ui-source:preview-refresh");
        ImGui::EndDisabled();
        if (gesture_ &&
            (gesture_->session != session_ || gesture_->revision != revision_ ||
             ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            gesture_.reset();
        if (!gesture_)
            try {
                preview_state(project, preview_width_, preview_height_, preview_density_, refresh);
            } catch (const std::exception& error) {
                if (!preview_frame_.state.is_object())
                    preview_frame_.state = Json::object();
                preview_frame_.state["stale"] = true;
                preview_frame_.state["error"] = error.what();
            }
        ImGui::BeginDisabled(gesture_active() || !preview_current() || !preview_frame_.image ||
                             !save_preview);
        if (button("Save preview image",
                   "Save this observed native preview through the image task."))
            attempt([&] { save_preview(); });
        FORGE_UI_PROBE("ui-source:preview-save-image");
        ImGui::EndDisabled();
        ImGui::BeginChild("ui-source-preview-properties", {0, 116}, ImGuiChildFlags_Borders);
        const auto* selected = preview_element(selected_);
        if (selected && preview_current()) {
            ImGui::Text("Selected: %s", selected->at("id").get<std::string>().c_str());
            if (selected->at("editable").get<bool>()) {
                const auto source_state = ProjectTextSource::digest(editor_.GetText());
                if (layout_controls_element_ != selected_ ||
                    layout_controls_state_ != source_state) {
                    const auto pixels =
                        read_ui_markup_layout(editor_.GetText(), outline_, selected_);
                    layout_controls_ = {pixels.left, pixels.top, pixels.width, pixels.height};
                    layout_controls_element_ = selected_;
                    layout_controls_state_ = source_state;
                }
                ImGui::BeginDisabled(gesture_active());
                ImGui::TextUnformatted("Left / Top / Width / Height (px)");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat4("##ui-source-layout-values", layout_controls_.data());
                FORGE_UI_PROBE("ui-source:layout-values");
                if (button("Apply layout",
                           "Replace the four existing pixel values as one Undo step."))
                    attempt([&] {
                        layout(selected_,
                               {layout_controls_[0], layout_controls_[1], layout_controls_[2],
                                layout_controls_[3]},
                               preview_frame_.state.at("instance").get<std::string>(),
                               preview_frame_.state.at("revision").get<std::uint64_t>());
                    });
                FORGE_UI_PROBE("ui-source:layout-apply");
                ImGui::EndDisabled();
            } else
                ImGui::TextWrapped("%s", selected->at("reason").get<std::string>().c_str());
        }
        if (!selected)
            ImGui::TextWrapped(
                "Select an element in the preview or Elements tab to inspect its layout.");
        ImGui::EndChild();
        if (gesture_) {
            auto& gesture = *gesture_;
            const auto mouse = ImGui::GetMousePos();
            const float dx = (mouse.x - gesture.pointer.x) / gesture.scale,
                        dy = (mouse.y - gesture.pointer.y) / gesture.scale;
            auto pixels = gesture.initial;
            if (gesture.resize) {
                pixels.width = std::clamp(pixels.width + dx, 1.f, 65536.f);
                pixels.height = std::clamp(pixels.height + dy, 1.f, 65536.f);
            } else {
                pixels.left = std::clamp(pixels.left + dx, -65536.f, 65536.f);
                pixels.top = std::clamp(pixels.top + dy, -65536.f, 65536.f);
            }
            if (!(pixels == gesture.pixels)) {
                gesture.pixels = pixels;
                gesture.valid = false;
                attempt([&] {
                    auto candidate = set_ui_markup_layout(gesture.source, gesture.element, pixels);
                    auto frame = request_preview(candidate, inspect_ui_markup(candidate), false);
                    bool editable = false;
                    if (frame.state.contains("elements"))
                        for (const auto& element : frame.state.at("elements"))
                            if (element.at("source_index") == gesture.element)
                                editable = element.at("editable").get<bool>();
                    gesture.valid = !frame.state.value("stale", true) && editable;
                    gesture.candidate = std::move(candidate);
                    gesture.frame = std::move(frame);
                });
            }
        }
        const auto& frame = gesture_ ? gesture_->frame : preview_frame_;
        ImGui::BeginChild("ui-source-preview-status", {0, 90});
        if (!preview_current() && !gesture_)
            ImGui::TextWrapped(
                "Preview is stale. Complete the source and refresh before editing layout.");
        ImGui::TextWrapped("Static draft preview. Gameplay bindings do not run. Drag to move; use "
                           "the bottom-right handle to resize. Escape cancels.");
        const auto error = frame.state.is_object() ? frame.state.value("error", "") : "";
        if (!error.empty()) {
            ImGui::TextWrapped("%s", error.c_str());
            if (editor_context)
                editor_context->problems.ingest({"ui-source-preview/" + session_,
                                                 "Error",
                                                 error,
                                                 {},
                                                 path_utf8(locator_),
                                                 {},
                                                 {}});
        }
        if (gesture_ && !gesture_->valid && error.empty() && frame.state.contains("elements"))
            for (const auto& element : frame.state.at("elements"))
                if (element.at("source_index") == gesture_->element && element.at("reason") != "")
                    ImGui::TextWrapped("%s", element.at("reason").get<std::string>().c_str());
        ImGui::EndChild();
        if (!frame.state.contains("width") || !frame.state.contains("height"))
            return;
        const float width = frame.state.at("width").get<float>(),
                    height = frame.state.at("height").get<float>();
        const auto available = ImGui::GetContentRegionAvail();
        const float scale = gesture_
                                ? gesture_->scale
                                : std::max(.05f, std::min({1.f, available.x / width,
                                                           std::max(120.f, available.y) / height}));
        const ImVec2 origin = ImGui::GetCursorScreenPos(), size{width * scale, height * scale};
        ImGui::InvisibleButton("ui-source-canvas", size, ImGuiButtonFlags_MouseButtonLeft);
        FORGE_UI_PROBE("ui-source:canvas");
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 end{origin.x + size.x, origin.y + size.y};
        if (frame.image)
            draw->AddImage(frame.image, origin, end);
        else {
            draw->AddRectFilled(origin, end, IM_COL32(9, 12, 16, 255));
            draw->AddText({origin.x + 8, origin.y + 8}, IM_COL32(220, 220, 220, 255),
                          "Preview image unavailable");
        }
        const Json* visual = nullptr;
        if (frame.state.contains("elements"))
            for (const auto& element : frame.state.at("elements"))
                if (element.at("source_index") == selected_)
                    visual = &element;
        ImVec2 handle{};
        if (visual) {
            const auto& bounds = visual->at("bounds");
            const ImVec2 begin{origin.x + bounds.at(0).get<float>() * scale,
                               origin.y + bounds.at(1).get<float>() * scale};
            handle = {begin.x + bounds.at(2).get<float>() * scale,
                      begin.y + bounds.at(3).get<float>() * scale};
            const auto color = visual->at("editable").get<bool>() ? IM_COL32(70, 195, 255, 255)
                                                                  : IM_COL32(245, 180, 65, 255);
            draw->AddRect(begin, handle, color, 0.f, 2.f);
            if (visual->at("editable").get<bool>())
                draw->AddRectFilled({handle.x - 5, handle.y - 5}, {handle.x + 5, handle.y + 5},
                                    color);
        }
        if (!gesture_ && preview_current() && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            const auto mouse = ImGui::GetMousePos();
            const bool resize = visual && visual->at("editable").get<bool>() &&
                                std::abs(mouse.x - handle.x) <= 8 &&
                                std::abs(mouse.y - handle.y) <= 8;
            if (!resize && preview_hit) {
                selected_ = UiMarkupElement::no_parent;
                const auto id =
                    preview_hit((mouse.x - origin.x) / scale, (mouse.y - origin.y) / scale);
                if (preview_frame_.state.contains("elements"))
                    for (const auto& element : preview_frame_.state.at("elements"))
                        if (element.at("id") == id && !element.at("source_index").is_null())
                            select(element.at("source_index").get<std::size_t>());
            }
            if (const auto* element = preview_element(selected_);
                element && element->at("editable").get<bool>()) {
                const auto text = editor_.GetText();
                const auto pixels = read_ui_markup_layout(text, outline_, selected_);
                gesture_ =
                    LayoutGesture{text,  session_, revision_, selected_, pixels, pixels,
                                  mouse, scale,    resize,    true,      text,   preview_frame_};
            }
        }
        if (gesture_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            auto gesture = std::move(*gesture_);
            gesture_.reset();
            if (gesture.valid && gesture.session == session_ && gesture.revision == revision_ &&
                gesture.source == editor_.GetText())
                attempt([&] {
                    replace_text(gesture.candidate);
                    select(gesture.element);
                });
        }
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
    // Composition callbacks borrow the existing native presentation owner.
    // A metadata-only host may return image0; source history remains here.
    std::function<UiSourcePreviewFrame(const std::filesystem::path&, std::string_view, int, int,
                                       float, bool)>
        preview;
    std::function<std::string(float, float)> preview_hit;
    std::function<void()> close_preview;
    std::function<void()> save_preview;
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
    bool gesture_active() const { return gesture_.has_value(); }
    bool can_undo() const { return is_open() && !gesture_ && editor_.CanUndo(); }
    bool can_redo() const { return is_open() && !gesture_ && editor_.CanRedo(); }
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
                {"gesture_active", gesture_active()},
                {"can_undo", can_undo()},
                {"can_redo", can_redo()},
                {"text", text},
                {"outline_error", outline_error_}};
    }
    const std::vector<UiMarkupElement>& outline() const { return outline_; }
    Json preview_state(SceneDocument& project, int width, int height, float density,
                       bool refresh = true) {
        check_project(project);
        if (gesture_ || close_)
            throw AuthoringError("source_busy", "Finish the source dialog or canvas gesture first");
        if (width < 1 || height < 1 || width > 2048 || height > 2048 || !std::isfinite(density) ||
            density < .5f || density > 4)
            throw AuthoringError("preview_dimensions", "Use1..2048 pixels and0.5..4 density");
        preview_width_ = width;
        preview_height_ = height;
        preview_density_ = density;
        try {
            preview_frame_ = request_preview(editor_.GetText(), outline_, refresh);
        } catch (const std::exception& error) {
            if (!preview_frame_.state.is_object())
                preview_frame_.state = Json::object();
            preview_frame_.state["stale"] = true;
            preview_frame_.state["error"] = error.what();
            throw;
        }
        return preview_frame_.state;
    }
    Json image_source(SceneDocument& project) {
        preview_state(project, preview_width_, preview_height_, preview_density_, false);
        if (!preview_current() || !preview_frame_.image)
            throw AuthoringError("preview_image_unavailable",
                                 "Preview the current source in a host with a native image first");
        return {{"path", path_utf8(locator_)},
                {"session", session_},
                {"revision", revision_},
                {"state", preview_frame_.state.at("source_state")},
                {"preview_instance", preview_frame_.state.at("instance")},
                {"preview_revision", preview_frame_.state.at("revision")},
                {"width", preview_frame_.state.at("width")},
                {"height", preview_frame_.state.at("height")},
                {"density", preview_frame_.state.at("density")}};
    }
    void layout(std::size_t index, UiPixelLayout pixels, const std::string& instance,
                std::uint64_t preview_revision) {
        if (gesture_)
            throw AuthoringError("source_busy", "Finish or cancel the canvas gesture first");
        // Reborrow the live owner: Play, project or presenter retirement can
        // invalidate a preview even when its source text has not changed.
        if (preview_current())
            try {
                preview_frame_ = request_preview(editor_.GetText(), outline_, false);
            } catch (...) {
                preview_frame_.state["stale"] = true;
                throw;
            }
        if (!preview_current() || preview_frame_.state.at("instance") != instance ||
            preview_frame_.state.at("revision") != preview_revision)
            throw AuthoringError("stale_ui_preview",
                                 "Preview the current source draft before editing layout");
        const auto* element = preview_element(index);
        if (!element || !element->at("editable").get<bool>())
            throw AuthoringError("layout_unsupported",
                                 element ? element->at("reason").get<std::string>()
                                         : "Element has no editable preview geometry");
        const auto candidate = set_ui_markup_layout(editor_.GetText(), index, pixels);
        try {
            preview_frame_ = request_preview(candidate, inspect_ui_markup(candidate), false);
        } catch (...) {
            preview_frame_.state["stale"] = true;
            throw;
        }
        element = preview_element(index);
        if (preview_frame_.state.value("stale", true) || !element ||
            !element->at("editable").get<bool>()) {
            preview_frame_.state["stale"] = true;
            throw AuthoringError("layout_unsupported",
                                 element ? element->at("reason").get<std::string>()
                                         : "Candidate has no editable preview geometry");
        }
        replace_text(candidate);
        select(index);
    }
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
        if (gesture_)
            throw AuthoringError("source_busy",
                                 "Finish or cancel the canvas gesture before saving");
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
        gesture_.reset();
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
        if (locked)
            gesture_.reset();
        editor_.SetReadOnly(locked);
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        bool visible = true;
        const bool contents = ImGui::Begin("UI Source###UI Source", &visible);
        const bool save_key = !locked && !gesture_ && save_shortcut && save_shortcut() &&
                              ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            context.task.focus_document("ui_source", "UI Source");
        if (!visible)
            request_close();
        if (contents && is_open()) {
            ImGui::TextWrapped("%s%s", path_utf8(locator_).c_str(), dirty_ ? " *" : "");
            ImGui::BeginDisabled(locked || gesture_active());
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
                    gesture_.reset();
                    editor_.Render("ui-source-text", ImGui::GetContentRegionAvail());
                    if (editor_.IsTextChanged())
                        changed();
                    ImGui::EndTabItem();
                }
                const bool elements_tab = ImGui::BeginTabItem("Elements");
                FORGE_UI_PROBE("ui-source:elements-tab");
                if (elements_tab) {
                    gesture_.reset();
                    element_controls(locked);
                    ImGui::EndTabItem();
                }
                const bool preview_tab = ImGui::BeginTabItem("Preview");
                FORGE_UI_PROBE("ui-source:preview-tab");
                if (preview_tab) {
                    preview_controls(project, locked);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            if (save_key && !gesture_)
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
