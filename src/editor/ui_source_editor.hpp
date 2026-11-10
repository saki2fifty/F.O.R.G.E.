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
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <type_traits>
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
    std::vector<UiStyleRule> rules_;
    std::size_t selected_rule_ = UiMarkupElement::no_parent;
    char rule_selector_[1025] = ".new-rule";
    std::vector<std::size_t> selected_;
    std::uint64_t revision_ = 0;
    bool dirty_ = false, close_ = false, focus_ = false, text_editable_ = false;
    char attribute_[65] = "id", value_[8193]{}, literal_[8193]{};
    char duplicate_prefix_[65]{};
    char style_name_[65] = "color", style_value_[8193]{};
    ImGuiTextFilter style_filter_;
    std::string style_error_, style_control_error_;
    bool style_mixed_ = false, classes_mixed_ = false;
    char classes_[1025]{};
    std::string classes_error_;
    std::size_t hierarchy_parent_ = UiMarkupElement::no_parent;
    std::size_t hierarchy_before_ = UiMarkupElement::no_parent;
    std::uint64_t drag_epoch_ = 0;
    bool outline_drag_active_ = false, elements_active_ = false;
    static constexpr const char* source_drag_type = "FORGE_UI_SOURCE";
    int child_kind_ = 0;
    char child_text_[8193]{};
    UiMarkupControlProperties control_property_view_;
    std::map<std::string, UiControlPropertyValue> control_property_values_, control_property_edits_;
    std::map<std::string, std::array<char, 8193>> control_property_text_;

    UiSourcePreviewFrame preview_frame_;
    int preview_width_ = 1000, preview_height_ = 700;
    float preview_density_ = 1;
    std::array<float, 4> layout_controls_{};
    std::string layout_controls_state_;
    std::size_t layout_controls_element_ = UiMarkupElement::no_parent;
    int group_action_ = 1;
    std::array<float, 2> group_offsets_{};
    struct LayoutGesture {
        std::string source, session;
        std::uint64_t revision;
        std::vector<UiMarkupLayoutEdit> initial, pixels;
        ImVec2 pointer;
        float scale = 1;
        bool resize = false, valid = false;
        std::string candidate;
        UiSourcePreviewFrame frame;
    };
    std::optional<LayoutGesture> gesture_;

    void changed() {
        gesture_.reset();
        outline_drag_active_ = false;
        const auto text = editor_.GetText();
        dirty_ = text != saved_;
        ++revision_;
        control_property_view_ = {};
        control_property_values_.clear();
        control_property_edits_.clear();
        control_property_text_.clear();
        selected_.clear();
        hierarchy_parent_ = hierarchy_before_ = UiMarkupElement::no_parent;
        outline_.clear();
        rules_.clear();
        selected_rule_ = UiMarkupElement::no_parent;
        outline_error_.clear();
        try {
            if (locator_.extension() == ".rml")
                outline_ = inspect_ui_markup(text);
            else if (locator_.extension() == ".rcss")
                rules_ = inspect_ui_stylesheet(text);
        } catch (const std::exception& error) {
            outline_error_ = error.what();
        }
    }
    void check_project(SceneDocument& project) const {
        project.check_ownership();
        if (!is_open() || root_ != project.project())
            throw AuthoringError("source_scope", "Open a UI source in the current project first");
    }
    void finish_close() {
        gesture_.reset();
        outline_drag_active_ = elements_active_ = false;
        ++drag_epoch_;
        preview_frame_ = {};
        if (close_preview)
            close_preview();
        control_property_view_ = {};
        control_property_values_.clear();
        control_property_edits_.clear();
        control_property_text_.clear();
        root_.clear();
        locator_.clear();
        disk_.clear();
        saved_.clear();
        session_.clear();
        outline_.clear();
        rules_.clear();
        selected_rule_ = UiMarkupElement::no_parent;
        error_.clear();
        outline_error_.clear();
        selected_.clear();
        style_error_.clear();
        style_control_error_.clear();
        classes_error_.clear();
        child_text_[0] = 0;
        style_mixed_ = classes_mixed_ = false;
        hierarchy_parent_ = hierarchy_before_ = UiMarkupElement::no_parent;
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
    std::size_t selected_element() const {
        return selected_.empty() ? UiMarkupElement::no_parent : selected_.back();
    }
    std::string element_label(std::string_view source, std::size_t index) const {
        const auto& node = outline_.at(index);
        auto label = "<" + node.tag + ">";
        for (const auto& attr : node.attributes)
            if (attr.name == "id" && attr.has_value) {
                label += " #" + Rml::StringUtilities::DecodeRml(std::string(source.substr(
                                    attr.value.begin, attr.value.end - attr.value.begin)));
                break;
            }
        return label;
    }
    void hierarchy_controls(bool locked) {
        const bool open = ImGui::CollapsingHeader("Move to container");
        FORGE_UI_PROBE("ui-source:hierarchy-open");
        if (!open)
            return;
        const auto source = editor_.GetText();
        const auto selected = [&](std::size_t i) {
            return std::ranges::find(selected_, i) != selected_.end();
        };
        ImGui::BeginDisabled(locked || std::ranges::any_of(selected_, [&](auto i) {
                                 return !ui_markup_widget_editable(outline_.at(i));
                             }));
        const auto parent_label = hierarchy_parent_ < outline_.size()
                                      ? element_label(source, hierarchy_parent_)
                                      : "Choose a container";
        const bool parent_open = ImGui::BeginCombo("New parent", parent_label.c_str());
        FORGE_UI_PROBE("ui-source:hierarchy-parent");
        if (parent_open) {
            for (std::size_t i = 0; i < outline_.size(); ++i) {
                const auto& node = outline_[i];
                if (node.self_closing || (node.tag != "body" && !ui_markup_widget_editable(node)) ||
                    std::ranges::any_of(selected_, [&](auto root) {
                        return node.source.begin >= outline_[root].source.begin &&
                               node.source.end <= outline_[root].source.end;
                    }))
                    continue;
                std::size_t depth = 0;
                for (auto ancestor = node.parent; ancestor != UiMarkupElement::no_parent;
                     ancestor = outline_[ancestor].parent)
                    ++depth;
                const auto label = std::string(depth * 2, ' ') + element_label(source, i);
                ImGui::PushID(int(i));
                if (ImGui::Selectable(label.c_str(), hierarchy_parent_ == i)) {
                    hierarchy_parent_ = i;
                    hierarchy_before_ = UiMarkupElement::no_parent;
                }
                FORGE_UI_PROBE("ui-source:parent:" + std::to_string(i));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(hierarchy_parent_ >= outline_.size());
        const auto before_label = hierarchy_before_ < outline_.size()
                                      ? "Before " + element_label(source, hierarchy_before_)
                                      : "At end";
        const bool before_open = ImGui::BeginCombo("Placement", before_label.c_str());
        FORGE_UI_PROBE("ui-source:hierarchy-before");
        if (before_open) {
            if (ImGui::Selectable("At end", hierarchy_before_ == UiMarkupElement::no_parent))
                hierarchy_before_ = UiMarkupElement::no_parent;
            FORGE_UI_PROBE("ui-source:before-end");
            for (std::size_t i = 0; i < outline_.size(); ++i)
                if (outline_[i].parent == hierarchy_parent_ && !selected(i)) {
                    const auto label = "Before " + element_label(source, i);
                    ImGui::PushID(int(i));
                    if (ImGui::Selectable(label.c_str(), hierarchy_before_ == i))
                        hierarchy_before_ = i;
                    FORGE_UI_PROBE("ui-source:before:" + std::to_string(i));
                    ImGui::PopID();
                }
            ImGui::EndCombo();
        }
        if (button("Move selection",
                   "Move static source siblings as one Undo step; keep their IDs."))
            attempt([&] {
                reparent(selected_, hierarchy_parent_,
                         hierarchy_before_ == UiMarkupElement::no_parent
                             ? std::nullopt
                             : std::optional(hierarchy_before_));
            });
        FORGE_UI_PROBE("ui-source:hierarchy-apply");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::TextWrapped("Styles and parent-relative positions follow the new container. "
                           "Name containers with IDs to identify them here.");
    }
    void read_style_controls() {
        style_error_.clear();
        style_control_error_.clear();
        style_mixed_ = false;
        style_value_[0] = 0;
        const bool sheet = locator_.extension() == ".rcss";
        if (sheet ? selected_rule_ >= rules_.size() : selected_element() >= outline_.size())
            return;
        try {
            std::optional<std::string> value;
            if (sheet)
                value = read_rule_property(selected_rule_, style_name_);
            else {
                const auto values = read_styles(selected_, style_name_);
                style_mixed_ = !std::ranges::all_of(
                    values, [&](const auto& v) { return v == values.front(); });
                if (style_mixed_)
                    return;
                value = values.front();
            }
            if (value) {
                if (value->size() >= sizeof(style_value_))
                    throw AuthoringError(
                        "source_capacity",
                        "This CSS value exceeds the control's8192-byte capacity; use Source");
                std::snprintf(style_value_, sizeof(style_value_), "%s", value->c_str());
            }
        } catch (const std::exception& error) {
            style_error_ = error.what();
        }
    }
    void style_value_controls() {
        const auto stage = [&](const auto& value) {
            try {
                const auto text = [&] {
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, UiStyleNumber>)
                        return format_ui_style_literal_number(style_name_, value);
                    else
                        return format_ui_style_color(value);
                }();
                std::snprintf(style_value_, sizeof(style_value_), "%s", text.c_str());
                style_control_error_.clear();
            } catch (const std::exception& error) {
                style_control_error_ = error.what();
            }
        };
        const std::string_view property(style_name_);
        const auto profile = ui_style_literal_profile(property);
        if (profile && !profile->keywords.empty()) {
            const auto current = std::string_view(style_value_);
            const bool known =
                std::ranges::find(profile->keywords, current) != profile->keywords.end();
            const bool open = ImGui::BeginCombo("Preset", known ? style_value_ : "Custom / unset");
            FORGE_UI_PROBE("ui-source:style-preset");
            if (open) {
                for (const auto keyword : profile->keywords) {
                    if (ImGui::Selectable(keyword.data(), current == keyword)) {
                        std::snprintf(style_value_, sizeof(style_value_), "%s", keyword.data());
                        style_control_error_.clear();
                    }
                    FORGE_UI_PROBE("ui-source:style-preset:" + std::string(keyword));
                }
                ImGui::EndCombo();
            }
        }
        if (profile && profile->color) {
            if (auto color = read_ui_style_color(style_value_)) {
                if (ImGui::ColorEdit4("Color", color->data(),
                                      ImGuiColorEditFlags_DisplayHex |
                                          ImGuiColorEditFlags_AlphaPreviewHalf |
                                          ImGuiColorEditFlags_NoOptions))
                    stage(*color);
                FORGE_UI_PROBE("ui-source:style-color");
            } else {
                ImGui::TextWrapped("Color control accepts #RRGGBB or #RRGGBBAA. Keep other CSS "
                                   "forms as text, or stage a new color.");
                if (ImGui::Button("New color"))
                    stage(UiStyleColor{1, 1, 1, 1});
                FORGE_UI_PROBE("ui-source:style-new-color");
            }
        } else if (profile && !profile->units.empty()) {
            if (auto number = read_ui_style_literal_number(property, style_value_)) {
                if (ImGui::InputDouble("Number", &number->value, 0, 0, "%.17g"))
                    stage(*number);
                FORGE_UI_PROBE("ui-source:style-number");
                const auto unit = static_cast<std::size_t>(number->unit);
                const auto units = ImGui::BeginCombo(
                    "Unit", unit == 0 ? "Unitless" : ui_style_unit_suffixes[unit].data());
                FORGE_UI_PROBE("ui-source:style-unit");
                if (units) {
                    for (const auto allowed_unit : profile->units) {
                        const auto i = static_cast<std::size_t>(allowed_unit);
                        if (ImGui::Selectable(i == 0 ? "Unitless"
                                                     : ui_style_unit_suffixes[i].data(),
                                              unit == i) &&
                            i != unit) {
                            number->unit = static_cast<UiStyleUnit>(i);
                            stage(*number);
                        }
                        FORGE_UI_PROBE("ui-source:style-unit:" + std::to_string(i));
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextWrapped("Keep custom CSS in CSS value, or stage a supported number.");
                if (ImGui::Button("New number"))
                    stage(profile->initial);
                FORGE_UI_PROBE("ui-source:style-new-number");
            }
        }
        if (!style_control_error_.empty())
            ImGui::TextWrapped("%s", style_control_error_.c_str());
    }
    void style_controls(bool locked, bool sheet = false) {
        const bool open = ImGui::CollapsingHeader(sheet                  ? "Rule properties"
                                                  : selected_.size() > 1 ? "Group inline style"
                                                                         : "Inline style");
        FORGE_UI_PROBE("ui-source:style-open");
        if (!open)
            return;
        ImGui::BeginDisabled(locked ||
                             (sheet ? !rules_.at(selected_rule_).flat
                                    : !ui_markup_widget_editable(outline_.at(selected_element()))));
        const bool choices = ImGui::BeginCombo("Common", style_name_);
        FORGE_UI_PROBE("ui-source:style-common");
        if (choices) {
            if (ImGui::IsWindowAppearing()) {
                style_filter_.Clear();
                ImGui::SetKeyboardFocusHere();
            }
            style_filter_.Draw("Search properties");
            FORGE_UI_PROBE("ui-source:style-filter");
            bool matched = false;
            for (const auto property : ui_common_style_properties) {
                const auto* name = property.data();
                if (!style_filter_.PassFilter(name))
                    continue;
                matched = true;
                if (ImGui::Selectable(name, std::string_view(style_name_) == name)) {
                    std::snprintf(style_name_, sizeof(style_name_), "%s", name);
                    read_style_controls();
                }
                FORGE_UI_PROBE("ui-source:style-choice:" + std::string(name));
            }
            if (!matched)
                ImGui::TextDisabled("No matching properties");
            ImGui::EndCombo();
        }
        if (ImGui::InputText("Property", style_name_, sizeof(style_name_))) {
            style_error_.clear();
            style_control_error_.clear();
            style_value_[0] = 0;
        }
        FORGE_UI_PROBE("ui-source:style-name");
        if (ImGui::InputTextWithHint("CSS value", "e.g. #77bdf7 or 24px", style_value_,
                                     sizeof(style_value_)))
            style_control_error_.clear();
        FORGE_UI_PROBE("ui-source:style-value");
        if (button(sheet ? "Read property" : "Read inline",
                   "Read the authored value; an empty field means no inline override."))
            read_style_controls();
        FORGE_UI_PROBE("ui-source:style-read");
        if (style_mixed_) {
            ImGui::TextWrapped(
                "Mixed authored values. Enter a value to apply to every selected sibling.");
            FORGE_UI_PROBE("ui-source:style-mixed");
        }
        if (!style_error_.empty())
            ImGui::TextWrapped("%s", style_error_.c_str());
        ImGui::BeginDisabled(!style_error_.empty());
        style_value_controls();
        ImGui::TextWrapped(
            "Controls stage the CSS value. Set property applies it as one Undo step.");
        ImGui::BeginDisabled(!style_control_error_.empty());
        if (button("Set property", "Replace only this CSS value as one source Undo step."))
            attempt([&] {
                if (sheet)
                    rule_property(selected_rule_, style_name_, std::string(style_value_));
                else
                    styles(selected_, style_name_, std::string(style_value_));
            });
        FORGE_UI_PROBE("ui-source:style-set");
        ImGui::SameLine();
        if (button("Remove property", "Remove this declaration; preserve surrounding source."))
            attempt([&] {
                if (sheet)
                    rule_property(selected_rule_, style_name_, {});
                else
                    styles(selected_, style_name_, {});
            });
        FORGE_UI_PROBE("ui-source:style-remove");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::TextWrapped(
            "%s", sheet ? "Authored stylesheet CSS. Save, then refresh an RML Preview or Reload UI "
                          "in Play. "
                          "Rule order, other styles and bindings affect the result."
                        : "Authored inline CSS. Preview tests native styling; Play tests bindings. "
                          "Other styles and bindings can affect the displayed result.");
    }
    void read_class_controls() {
        classes_[0] = 0;
        classes_mixed_ = false;
        classes_error_.clear();
        if (selected_.empty())
            return;
        try {
            const auto values = read_classes(selected_);
            classes_mixed_ =
                !std::ranges::all_of(values, [&](const auto& v) { return v == values.front(); });
            if (classes_mixed_)
                return;
            if (const auto& value = values.front(); value) {
                if (value->size() >= sizeof(classes_))
                    throw AuthoringError("source_capacity",
                                         "This class list exceeds1024 bytes; use Source");
                std::snprintf(classes_, sizeof(classes_), "%s", value->c_str());
            }
        } catch (const std::exception& error) {
            classes_error_ = error.what();
        }
    }
    void class_controls(bool locked) {
        const bool open = ImGui::CollapsingHeader("Classes");
        FORGE_UI_PROBE("ui-source:classes-open");
        if (!open)
            return;
        ImGui::BeginDisabled(locked);
        if (ImGui::InputText("Class list", classes_, sizeof(classes_)))
            classes_error_.clear();
        FORGE_UI_PROBE("ui-source:classes-value");
        if (button("Read classes",
                   "Read authored class lists; mixed values leave the field blank."))
            read_class_controls();
        FORGE_UI_PROBE("ui-source:classes-read");
        if (classes_mixed_) {
            ImGui::TextWrapped(
                "Mixed authored class lists. Set classes replaces all selected lists.");
            FORGE_UI_PROBE("ui-source:classes-mixed");
        }
        if (!classes_error_.empty())
            ImGui::TextWrapped("%s", classes_error_.c_str());
        ImGui::BeginDisabled(!classes_error_.empty());
        ImGui::BeginDisabled(classes_[0] == 0);
        if (button("Set classes",
                   "Set the literal class list for all selected siblings in one Undo step."))
            attempt([&] { classes(selected_, std::string(classes_)); });
        FORGE_UI_PROBE("ui-source:classes-set");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (button("Clear classes",
                   "Remove authored class attributes; retain runtime class bindings."))
            attempt([&] { classes(selected_, {}); });
        FORGE_UI_PROBE("ui-source:classes-clear");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::TextWrapped("Space-separated literal classes. Set replaces the entire list; runtime "
                           "bindings can override it.");
    }
    void replace_selected(const std::string& candidate, std::vector<std::size_t> retained) {
        const auto before = revision_;
        replace_text(candidate);
        if (revision_ != before) {
            selected_ = std::move(retained);
            selection_controls();
        }
    }
    void select_rule(std::size_t index) {
        if (index >= rules_.size())
            throw AuthoringError("source_rule", "Choose a current stylesheet rule");
        selected_rule_ = index;
        const auto text = editor_.GetText();
        const auto range = rules_[index].selector;
        const auto selector = text.substr(range.begin, range.end - range.begin);
        std::snprintf(rule_selector_, sizeof(rule_selector_), "%s",
                      selector.size() < sizeof(rule_selector_) ? selector.c_str() : "");
        read_style_controls();
    }
    void rule_controls(bool locked) {
        if (!outline_error_.empty()) {
            ImGui::TextWrapped("Rules unavailable: %s. Continue in Source.",
                               outline_error_.c_str());
            return;
        }
        const auto snapshot = editor_.GetText();
        if (ImGui::BeginListBox("Stylesheet rules", {0, 140})) {
            for (std::size_t i = 0; i < rules_.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const auto range = rules_[i].selector;
                const auto label = snapshot.substr(range.begin, range.end - range.begin);
                if (ImGui::Selectable(label.c_str(), selected_rule_ == i))
                    select_rule(i);
                FORGE_UI_PROBE("ui-source:rule:" + std::to_string(i));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", label.c_str());
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        ImGui::BeginDisabled(locked);
        ImGui::InputText("Selector", rule_selector_, sizeof(rule_selector_));
        FORGE_UI_PROBE("ui-source:rule-selector");
        if (button("Add rule", "Append an empty flat rule. Preserve existing order and source."))
            attempt([&] { add_rule(rule_selector_); });
        FORGE_UI_PROBE("ui-source:rule-add");
        ImGui::EndDisabled();
        if (selected_rule_ >= rules_.size()) {
            ImGui::TextWrapped("Select a flat rule to edit its declarations, or add a selector.");
            return;
        }
        if (!rules_[selected_rule_].flat) {
            ImGui::TextWrapped("At-rules and nested blocks stay in Source.");
            return;
        }
        const bool long_selector =
            rules_[selected_rule_].selector.end - rules_[selected_rule_].selector.begin >=
            sizeof(rule_selector_);
        ImGui::BeginDisabled(locked || long_selector);
        if (button("Set selector", "Replace this selector, preserving its rule body."))
            attempt([&] { rule_selector(selected_rule_, rule_selector_); });
        FORGE_UI_PROBE("ui-source:rule-rename");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(locked);
        if (button("Remove rule", "Remove this flat block as one source Undo step."))
            attempt([&] { remove_rule(selected_rule_); });
        FORGE_UI_PROBE("ui-source:rule-remove");
        ImGui::EndDisabled();
        if (long_selector)
            ImGui::TextWrapped(
                "This selector exceeds the control's1024-byte capacity; use Source.");
        if (selected_rule_ >= rules_.size())
            return;
        ImGui::BeginDisabled(locked || rules_.size() >= 4096);
        if (button("Copy rule",
                   "Copy this flat block with its selector unchanged; order affects the cascade."))
            attempt([&] { copy_rule(selected_rule_); });
        FORGE_UI_PROBE("ui-source:rule-copy");
        ImGui::EndDisabled();
        const auto index = selected_rule_;
        ImGui::BeginDisabled(locked || index == 0 || !rules_[index - 1].flat);
        if (button("Move rule up",
                   "Swap adjacent flat rules; outside comments remain between them."))
            attempt([&] { move_rule(selected_rule_, -1); });
        FORGE_UI_PROBE("ui-source:rule-up");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(locked || index + 1 >= rules_.size() || !rules_[index + 1].flat);
        if (button("Move rule down", "Swap adjacent flat rules as one source Undo step."))
            attempt([&] { move_rule(selected_rule_, 1); });
        FORGE_UI_PROBE("ui-source:rule-down");
        ImGui::EndDisabled();
        style_controls(locked, true);
    }
    void reset_control_property_value(const UiMarkupControlProperty& row) {
        auto value = row.value;
        if (!row.available) {
            if (row.type == UiControlPropertyType::Text)
                value = std::string{};
            else if (row.type == UiControlPropertyType::Flag)
                value = false;
            else if (row.type == UiControlPropertyType::Number)
                value = 0.0;
            else
                value = std::monostate{};
        }
        control_property_values_[row.name] = value;
        if (row.type == UiControlPropertyType::Text) {
            auto& buffer = control_property_text_[row.name];
            const auto* text = std::get_if<std::string>(&value);
            std::snprintf(buffer.data(), buffer.size(), "%s", text ? text->c_str() : "");
        }
    }
    void discard_control_property_edits() {
        control_property_edits_.clear();
        control_property_values_.clear();
        control_property_text_.clear();
        for (const auto& row : control_property_view_.properties)
            reset_control_property_value(row);
    }
    void read_control_property_controls() {
        control_property_view_ = {};
        control_property_edits_.clear();
        control_property_values_.clear();
        control_property_text_.clear();
        const auto index = selected_element();
        if (selected_.size() != 1 || index >= outline_.size())
            return;
        try {
            control_property_view_ =
                read_ui_markup_control_properties(editor_.GetText(), outline_, index);
            discard_control_property_edits();
        } catch (const std::exception& error) {
            control_property_view_.reason = error.what();
        }
    }
    bool control_property_controls(std::size_t index, bool locked) {
        if (control_property_view_.properties.empty() ||
            !ImGui::CollapsingHeader("Control properties", ImGuiTreeNodeFlags_DefaultOpen))
            return false;
        ImGui::PushID("source-control-properties");
        ImGui::TextUnformatted(control_property_view_.label.c_str());
        ImGui::TextWrapped(
            "Authored source values. Changes are staged: Apply properties, then Save source.");
        if (control_property_view_.kind == "image")
            ImGui::TextWrapped(
                "A nonempty sprite takes precedence over source and crop. "
                "Dimensions are authored attributes; CSS and the loaded image "
                "determine the displayed size. Zero leaves native intrinsic sizing.");
        for (const auto& row : control_property_view_.properties) {
            ImGui::PushID(row.name.c_str());
            ImGui::Separator();
            if (row.type != UiControlPropertyType::Flag || !row.editable)
                ImGui::TextUnformatted(row.label.c_str());
            if (row.integer && row.minimum && row.maximum)
                ImGui::TextDisabled("Whole number: %.0f..%.0f", *row.minimum, *row.maximum);
            if (row.minimum_positive)
                ImGui::TextDisabled("Positive minimum: %.6g", *row.minimum_positive);
            const auto edit = control_property_edits_.find(row.name);
            const bool reset = edit != control_property_edits_.end() &&
                               std::holds_alternative<std::monostate>(edit->second);
            if (edit != control_property_edits_.end())
                ImGui::TextDisabled(reset ? "Override will be removed on Apply" : "Staged change");
            else {
                if (!row.authored && row.available)
                    ImGui::TextDisabled(std::holds_alternative<std::monostate>(row.value)
                                            ? "No inline override"
                                            : "Native default");
                if (!row.reason.empty())
                    ImGui::TextWrapped("%s", row.reason.c_str());
            }
            if (row.editable) {
                auto& value = control_property_values_.at(row.name);
                bool edited = false;
                ImGui::BeginDisabled(locked || reset);
                if (!row.available && edit == control_property_edits_.end())
                    ImGui::TextDisabled("Enter a replacement");
                ImGui::SetNextItemWidth(-1);
                if (row.type == UiControlPropertyType::Flag) {
                    edited = ImGui::Checkbox(row.label.c_str(), &std::get<bool>(value));
                } else if (row.type == UiControlPropertyType::Number) {
                    edited = ImGui::InputDouble("##value", &std::get<double>(value), 0, 0, "%.6g");
                } else if (row.type == UiControlPropertyType::Text) {
                    auto& buffer = control_property_text_.at(row.name);
                    if (std::string_view(buffer.data()).find('\n') != std::string_view::npos)
                        edited = ImGui::InputTextMultiline("##value", buffer.data(), buffer.size(),
                                                           {0, 48});
                    else
                        edited = ImGui::InputText("##value", buffer.data(), buffer.size());
                    if (edited)
                        value = std::string(buffer.data());
                } else {
                    const auto* text = std::get_if<std::string>(&value);
                    const bool open =
                        ImGui::BeginCombo("##value", text            ? text->c_str()
                                                     : row.available ? "No inline override"
                                                                     : "Choose replacement");
                    FORGE_UI_PROBE("ui-source:control-property:" + row.name);
                    if (open) {
                        for (const auto& choice : row.choices) {
                            if (ImGui::Selectable(choice.c_str(), text && *text == choice)) {
                                value = choice;
                                edited = true;
                            }
                            FORGE_UI_PROBE("ui-source:control-choice:" + row.name + ":" + choice);
                        }
                        ImGui::EndCombo();
                    }
                }
                if (row.type != UiControlPropertyType::Choice)
                    FORGE_UI_PROBE("ui-source:control-property:" + row.name);
                ImGui::EndDisabled();
                if (edited)
                    control_property_edits_[row.name] = value;
            }
            ImGui::BeginDisabled(locked || !row.editable ||
                                 (!row.authored && edit == control_property_edits_.end()));
            const bool reset_clicked = button(reset ? "Keep override" : "Reset property",
                                              "Stage removal of this authored attribute or inline "
                                              "style; other source stays unchanged.");
            FORGE_UI_PROBE("ui-source:control-reset:" + row.name);
            ImGui::EndDisabled();
            if (reset_clicked) {
                if (reset) {
                    reset_control_property_value(row);
                    control_property_edits_.erase(row.name);
                } else
                    control_property_edits_[row.name] = std::monostate{};
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::BeginDisabled(locked || control_property_edits_.empty());
        const bool apply =
            button("Apply properties",
                   "Publish the complete control-property candidate as one source Undo step.");
        FORGE_UI_PROBE("ui-source:control-apply");
        const bool discard =
            button("Discard staged properties",
                   "Restore this panel's values without changing the source draft.");
        FORGE_UI_PROBE("ui-source:control-discard");
        ImGui::EndDisabled();
        ImGui::PopID();
        if (discard)
            discard_control_property_edits();
        if (apply) {
            const auto changes = control_property_edits_;
            attempt([&] {
                control_values(index, changes);
                discard_control_property_edits();
            });
            return true; // The draft owner may have rebuilt all outline/property observations.
        }
        return false;
    }

    void selection_controls() {
        const auto index = selected_element();
        if (index == UiMarkupElement::no_parent)
            return;
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
        read_style_controls();
        read_class_controls();
        read_control_property_controls();
    }
    void select(std::size_t index, bool toggle = false) {
        if (index >= outline_.size())
            throw AuthoringError("source_element", "Select a current source element");
        hierarchy_parent_ = hierarchy_before_ = UiMarkupElement::no_parent;
        if (!toggle)
            selected_.assign(1, index);
        else if (const auto found = std::ranges::find(selected_, index); found != selected_.end())
            selected_.erase(found);
        else {
            if (selected_.size() == ui_source_selection_limit)
                throw AuthoringError("layout_selection", "Select at most32 elements");
            selected_.push_back(index);
        }
        selection_controls();
    }
    void select_layouts(std::span<const UiMarkupLayoutEdit> changes) {
        selected_.clear();
        for (const auto& change : changes)
            selected_.push_back(change.element);
        selection_controls();
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
    void check_preview_observation(const std::string& instance, std::uint64_t revision) const {
        if (!preview_current() || preview_frame_.state.at("instance") != instance ||
            preview_frame_.state.at("revision") != revision)
            throw AuthoringError("stale_ui_preview",
                                 "Preview the current source draft before editing layout");
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
    std::vector<UiMarkupLayoutBox> layout_boxes(std::span<const std::size_t> indices) const {
        if (indices.empty() || indices.size() > ui_source_selection_limit)
            throw AuthoringError("layout_selection", "Choose1..32 elements for layout");
        const auto source = editor_.GetText();
        std::optional<std::size_t> parent;
        std::vector<UiMarkupLayoutBox> result;
        result.reserve(indices.size());
        for (const auto index : indices) {
            const auto* native = preview_element(index);
            if (!native || !native->at("editable").get<bool>())
                throw AuthoringError("layout_unsupported",
                                     native ? native->at("reason").get<std::string>()
                                            : "Element has no editable preview geometry");
            if (std::ranges::any_of(result,
                                    [&](const auto& box) { return box.source.element == index; }))
                throw AuthoringError("layout_selection", "Group elements must be distinct");
            const auto current_parent = outline_.at(index).parent;
            if (parent && *parent != current_parent)
                throw AuthoringError("layout_unsupported",
                                     "Group layout requires elements with the same source parent");
            parent = current_parent;
            const auto& bounds = native->at("bounds");
            result.push_back({{index, read_ui_markup_layout(source, outline_, index)},
                              {bounds.at(0).get<float>(), bounds.at(1).get<float>(),
                               bounds.at(2).get<float>(), bounds.at(3).get<float>()}});
        }
        return result;
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
        ImGui::BeginChild("ui-source-preview-properties", {0, selected_.size() > 1 ? 200.f : 116.f},
                          ImGuiChildFlags_Borders);
        const auto* selected = preview_element(selected_element());
        if (selected_.size() > 1 && preview_current()) {
            ImGui::Text("%zu selected | Ctrl+click to toggle", selected_.size());
            const auto& action = ui_layout_group_actions.at(std::size_t(group_action_));
            if (ImGui::BeginCombo("Operation", action.second.data())) {
                for (std::size_t i = 0; i < ui_layout_group_actions.size(); ++i) {
                    if (ImGui::Selectable(ui_layout_group_actions[i].second.data(),
                                          group_action_ == int(i)))
                        group_action_ = int(i);
                    FORGE_UI_PROBE("ui-source:group-choice:" +
                                   std::string(ui_layout_group_actions[i].first));
                }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("ui-source:group-action");
            const auto key = ui_layout_group_actions.at(std::size_t(group_action_)).first;
            if (key == "move") {
                ImGui::InputFloat2("Delta X / Y (px)", group_offsets_.data());
                FORGE_UI_PROBE("ui-source:group-offsets");
            }
            ImGui::BeginDisabled(gesture_active() ||
                                 (key.starts_with("distribute_") && selected_.size() < 3));
            if (button("Apply group", "Validate all rectangles, then make one source Undo step."))
                attempt([&] {
                    const auto indices = selected_;
                    arrange(indices, key, key == "move" ? group_offsets_[0] : 0,
                            key == "move" ? group_offsets_[1] : 0,
                            preview_frame_.state.at("instance").get<std::string>(),
                            preview_frame_.state.at("revision").get<std::uint64_t>());
                });
            FORGE_UI_PROBE("ui-source:group-apply");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(gesture_active());
            if (button("Remove group", "Remove the selected source siblings as one Undo step."))
                attempt([&] { removes(selected_); });
            FORGE_UI_PROBE("ui-source:group-remove");
            ImGui::EndDisabled();
            ImGui::TextWrapped(
                "Use sibling absolute-pixel elements. Drag a selected member to move "
                "the group. Resize handles are available for a single element.");
        } else if (selected && preview_current()) {
            ImGui::Text("Selected: %s", selected->at("id").get<std::string>().c_str());
            if (selected->at("editable").get<bool>()) {
                const auto source_state = ProjectTextSource::digest(editor_.GetText());
                if (layout_controls_element_ != selected_element() ||
                    layout_controls_state_ != source_state) {
                    const auto pixels =
                        read_ui_markup_layout(editor_.GetText(), outline_, selected_element());
                    layout_controls_ = {pixels.left, pixels.top, pixels.width, pixels.height};
                    layout_controls_element_ = selected_element();
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
                        layout(selected_element(),
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
        if (!selected && selected_.size() < 2)
            ImGui::TextWrapped(
                "Select an element in the preview or Elements tab to inspect its layout.");
        ImGui::EndChild();
        if (gesture_) {
            auto& gesture = *gesture_;
            const auto mouse = ImGui::GetMousePos();
            float dx = (mouse.x - gesture.pointer.x) / gesture.scale,
                  dy = (mouse.y - gesture.pointer.y) / gesture.scale;
            auto pixels = gesture.initial;
            if (gesture.resize) {
                auto& value = pixels.front().pixels;
                value.width = std::clamp(value.width + dx, 1.f, 65536.f);
                value.height = std::clamp(value.height + dy, 1.f, 65536.f);
            } else {
                float min_x = -131072, max_x = 131072, min_y = -131072, max_y = 131072;
                for (const auto& value : pixels) {
                    min_x = std::max(min_x, -65536 - value.pixels.left);
                    max_x = std::min(max_x, 65536 - value.pixels.left);
                    min_y = std::max(min_y, -65536 - value.pixels.top);
                    max_y = std::min(max_y, 65536 - value.pixels.top);
                }
                dx = std::clamp(dx, min_x, max_x);
                dy = std::clamp(dy, min_y, max_y);
                for (auto& value : pixels) {
                    value.pixels.left += dx;
                    value.pixels.top += dy;
                }
            }
            if (!(pixels == gesture.pixels)) {
                gesture.pixels = pixels;
                gesture.valid = false;
                attempt([&] {
                    auto candidate = set_ui_markup_layouts(gesture.source, pixels);
                    auto frame = request_preview(candidate, inspect_ui_markup(candidate), false);
                    const bool editable =
                        frame.state.contains("elements") &&
                        std::ranges::all_of(pixels, [&](const auto& change) {
                            for (const auto& element : frame.state.at("elements"))
                                if (element.at("source_index") == change.element)
                                    return element.at("editable").template get<bool>();
                            return false;
                        });
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
        ImGui::TextWrapped("Static draft preview. Gameplay bindings do not run. Ctrl+click toggles "
                           "group selection; drag to move. A single element has a resize handle. "
                           "Escape cancels.");
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
                if (element.at("reason") != "" &&
                    std::ranges::any_of(gesture_->initial, [&](const auto& change) {
                        return element.at("source_index") == change.element;
                    }))
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
        ImVec2 handle{};
        if (frame.state.contains("elements"))
            for (const auto& element : frame.state.at("elements")) {
                if (element.at("source_index").is_null() ||
                    std::ranges::find(selected_, element.at("source_index").get<std::size_t>()) ==
                        selected_.end())
                    continue;
                if (element.at("source_index") == selected_element())
                    visual = &element;
                const auto& bounds = element.at("bounds");
                const ImVec2 begin{origin.x + bounds.at(0).get<float>() * scale,
                                   origin.y + bounds.at(1).get<float>() * scale};
                const ImVec2 corner{begin.x + bounds.at(2).get<float>() * scale,
                                    begin.y + bounds.at(3).get<float>() * scale};
                const auto color = element.at("editable").get<bool>() ? IM_COL32(70, 195, 255, 255)
                                                                      : IM_COL32(245, 180, 65, 255);
                draw->AddRect(begin, corner, color, 0.f, 2.f);
                if (selected_.size() == 1 && element.at("editable").get<bool>()) {
                    handle = corner;
                    draw->AddRectFilled({handle.x - 5, handle.y - 5}, {handle.x + 5, handle.y + 5},
                                        color);
                }
            }
        if (!gesture_ && preview_current() && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            const auto mouse = ImGui::GetMousePos();
            const bool resize =
                selected_.size() == 1 && visual && visual->at("editable").get<bool>() &&
                std::abs(mouse.x - handle.x) <= 8 && std::abs(mouse.y - handle.y) <= 8;
            if (!resize && preview_hit) {
                std::optional<std::size_t> hit;
                const auto id =
                    preview_hit((mouse.x - origin.x) / scale, (mouse.y - origin.y) / scale);
                if (preview_frame_.state.contains("elements"))
                    for (const auto& element : preview_frame_.state.at("elements"))
                        if (element.at("id") == id && !element.at("source_index").is_null())
                            hit = element.at("source_index").get<std::size_t>();
                if (ImGui::GetIO().KeyCtrl) {
                    if (hit)
                        attempt([&] { select(*hit, true); });
                    return; // A toggle chooses membership, without starting a drag.
                }
                if (!hit)
                    selected_.clear();
                else if (std::ranges::find(selected_, *hit) == selected_.end())
                    select(*hit);
            }
            if (!selected_.empty())
                attempt([&] {
                    const auto text = editor_.GetText();
                    std::vector<UiMarkupLayoutEdit> pixels;
                    for (const auto& box : layout_boxes(selected_))
                        pixels.push_back(box.source);
                    gesture_ =
                        LayoutGesture{text,  session_, revision_, pixels, pixels,        mouse,
                                      scale, resize,   true,      text,   preview_frame_};
                });
        }
        if (gesture_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            auto gesture = std::move(*gesture_);
            gesture_.reset();
            if (gesture.valid && gesture.session == session_ && gesture.revision == revision_ &&
                gesture.source == editor_.GetText())
                attempt([&] {
                    replace_text(gesture.candidate);
                    select_layouts(gesture.pixels);
                });
        }
    }
    Json drag_observation(const ImGuiPayload& payload) const {
        if (payload.DataSize < 2 || payload.DataSize > 1024 ||
            static_cast<const char*>(payload.Data)[payload.DataSize - 1] != 0)
            throw AuthoringError("stale_ui_source", "Start a new source outline drag");
        auto request = Json::parse(std::string_view(static_cast<const char*>(payload.Data),
                                                    std::size_t(payload.DataSize - 1)));
        if (!request.is_object() || !request.contains("revision") ||
            !request.at("revision").is_number_unsigned() || !request.contains("epoch") ||
            !request.at("epoch").is_number_unsigned() ||
            request.value("session", std::string{}) != session_ ||
            request.value("revision", std::uint64_t(-1)) != revision_ ||
            request.value("epoch", std::uint64_t(-1)) != drag_epoch_ ||
            !request.contains("elements") || !request.at("elements").is_array() ||
            request.at("elements").empty() ||
            request.at("elements").size() > ui_source_selection_limit ||
            !std::ranges::all_of(request.at("elements"), [&](const auto& index) {
                return index.is_number_unsigned() &&
                       index.template get<std::uint64_t>() < outline_.size();
            }))
            throw AuthoringError("stale_ui_source", "Source changed; start a new outline drag");
        return request;
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
        std::optional<Json> dropped;
        const auto draw_node = [&](auto&& self, std::size_t& cursor) -> void {
            const auto index = cursor++;
            const auto& node = outline_[index];
            const bool leaf = cursor == outline_.size() || outline_[cursor].parent != index;
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (node.tag == "body")
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            if (leaf)
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (std::ranges::find(selected_, index) != selected_.end())
                flags |= ImGuiTreeNodeFlags_Selected;
            const auto label = element_label(snapshot, index);
            const bool open =
                ImGui::TreeNodeEx(reinterpret_cast<void*>(index + 1), flags, "%s", label.c_str());
            FORGE_UI_PROBE("ui-source:element:" + std::to_string(index));
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() &&
                (ImGui::GetIO().KeyCtrl || selected_.size() <= 1 ||
                 std::ranges::find(selected_, index) == selected_.end()))
                attempt([&] { select(index, ImGui::GetIO().KeyCtrl); });
            if (!locked && !selected_.empty() &&
                std::ranges::find(selected_, index) != selected_.end() &&
                ui_markup_widget_editable(node) &&
                std::ranges::all_of(
                    selected_, [&](auto i) { return ui_markup_widget_editable(outline_.at(i)); }) &&
                ImGui::BeginDragDropSource()) {
                const auto payload = Json{
                    {"session", session_},
                    {"revision", revision_},
                    {"epoch", drag_epoch_},
                    {"elements",
                     selected_}}.dump();
                ImGui::SetDragDropPayload(source_drag_type, payload.c_str(), payload.size() + 1,
                                          ImGuiCond_Once);
                ImGui::TextUnformatted("Move static source selection");
                ImGui::TextUnformatted("Drop into a container; hold Shift to place before a row.");
                ImGui::EndDragDropSource();
            }
            const bool before = ImGui::GetIO().KeyShift;
            const auto parent = before ? node.parent : index;
            const bool container =
                parent < outline_.size() && !outline_[parent].self_closing &&
                (outline_[parent].tag == "body" || ui_markup_widget_editable(outline_[parent]));
            if (!locked && container && ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload(
                        source_drag_type, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                    try {
                        auto request = drag_observation(*payload);
                        ImGui::SetTooltip("%s %s", before ? "Place before" : "Move into",
                                          label.c_str());
                        if (payload->IsDelivery()) {
                            request["parent"] = parent;
                            if (before)
                                request["before"] = index;
                            dropped = std::move(request);
                        }
                    } catch (const std::exception& error) {
                        ImGui::SetTooltip("%s", error.what());
                    }
                }
                ImGui::EndDragDropTarget();
            }
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
        if (dropped) {
            // Commit after traversal; rebuilding the outline invalidates row references.
            outline_drag_active_ = false;
            ++drag_epoch_;
            attempt([&] {
                reparent(dropped->at("elements").get<std::vector<std::size_t>>(),
                         dropped->at("parent").get<std::size_t>(),
                         dropped->contains("before")
                             ? std::optional(dropped->at("before").get<std::size_t>())
                             : std::nullopt);
            });
            return;
        }
        if (selected_element() >= outline_.size()) {
            ImGui::TextWrapped("Select a container or widget to edit its source.");
            return;
        }
        hierarchy_controls(locked);
        if (selected_element() >= outline_.size())
            return;
        class_controls(locked);
        style_controls(locked);
        if (selected_element() >= outline_.size())
            return;
        if (selected_.size() > 1) {
            ImGui::Separator();
            ImGui::Text("%zu source elements selected", selected_.size());
            ImGui::BeginDisabled(locked);
            if (button("Remove selection", "Remove all selected siblings as one source Undo step."))
                attempt([&] { removes(selected_); });
            FORGE_UI_PROBE("ui-source:selection-remove");
            ImGui::EndDisabled();
            ImGui::TextWrapped(
                "Styles and classes apply to all selected siblings. Use Preview for group layout; "
                "select one element for other attributes or children.");
            return;
        }
        // Snapshot affordances before an edit can rebuild the derived outline.
        const auto index = selected_element();
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
        if (control_property_controls(index, locked))
            return;

        ImGui::BeginDisabled(locked);
        ImGui::BeginDisabled(!widget_editable);
        ImGui::InputTextWithHint("ID prefix", "Automatic", duplicate_prefix_,
                                 sizeof(duplicate_prefix_));
        FORGE_UI_PROBE("ui-source:duplicate-prefix");
        if (button("Duplicate subtree",
                   "Copy static markup with new IDs; existing IDs stay unchanged."))
            attempt([&] { duplicate(index, duplicate_prefix_); });
        FORGE_UI_PROBE("ui-source:element-duplicate");
        ImGui::EndDisabled();
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
        ImGui::BeginDisabled(!container || tag == "textarea" || tag == "option" ||
                             tag == "progress");
        const auto choices =
            ImGui::BeginCombo("New child", ui_markup_control_kinds[child_kind_].label.data());
        FORGE_UI_PROBE("ui-source:child-kind");
        if (choices) {
            for (std::size_t i = 0; i < ui_markup_control_kinds.size(); ++i) {
                const auto& kind = ui_markup_control_kinds[i];
                if (ImGui::Selectable(kind.label.data(), child_kind_ == static_cast<int>(i)))
                    child_kind_ = static_cast<int>(i);
                FORGE_UI_PROBE("ui-source:child-choice:" + std::string(kind.id));
            }
            ImGui::EndCombo();
        }
        const auto& kind = ui_markup_control_kinds[child_kind_];
        ImGui::BeginDisabled(!kind.accepts_text());
        ImGui::InputTextWithHint("Initial text/value", "Optional; escaped as source text",
                                 child_text_, sizeof(child_text_));
        FORGE_UI_PROBE("ui-source:child-text");
        ImGui::EndDisabled();
        const bool dropdown = (kind.tag == "option") == (tag == "select");
        ImGui::BeginDisabled(!dropdown);
        if (button("Add child",
                   "Create the chosen control as one Undo and select it. Save remains separate."))
            attempt([&] { control(index, kind.id, kind.accepts_text() ? child_text_ : ""); });
        FORGE_UI_PROBE("ui-source:child-add");
        ImGui::EndDisabled();
        if (!dropdown)
            ImGui::TextWrapped("Create dropdown options inside a Dropdown; other controls use a "
                               "different container.");
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
    bool gesture_active() const { return gesture_.has_value() || outline_drag_active_; }
    bool can_undo() const { return is_open() && !gesture_active() && editor_.CanUndo(); }
    bool can_redo() const { return is_open() && !gesture_active() && editor_.CanRedo(); }
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
                {"selection", selected_},
                {"rule_selection",
                 selected_rule_ < rules_.size() ? Json(selected_rule_) : Json(nullptr)},
                {"can_undo", can_undo()},
                {"can_redo", can_redo()},
                {"text", text},
                {"outline_error", outline_error_}};
    }
    const std::vector<UiMarkupElement>& outline() const { return outline_; }
    const std::vector<UiStyleRule>& rules() const { return rules_; }
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
        const std::array changes{UiMarkupLayoutEdit{index, pixels}};
        layouts(changes, instance, preview_revision);
    }
    void layouts(std::span<const UiMarkupLayoutEdit> changes, const std::string& instance,
                 std::uint64_t preview_revision) {
        if (changes.empty() || changes.size() > ui_source_selection_limit)
            throw AuthoringError("layout_selection", "Choose1..32 elements for a layout edit");
        if (gesture_active())
            throw AuthoringError("source_busy", "Finish or cancel the source gesture first");
        // Reborrow the live owner: Play, project or presenter retirement can
        // invalidate a preview even when its source text has not changed.
        if (preview_current())
            try {
                preview_frame_ = request_preview(editor_.GetText(), outline_, false);
            } catch (...) {
                preview_frame_.state["stale"] = true;
                throw;
            }
        check_preview_observation(instance, preview_revision);
        std::vector<std::size_t> indices;
        indices.reserve(changes.size());
        for (const auto& change : changes)
            indices.push_back(change.element);
        layout_boxes(indices);
        const auto candidate = set_ui_markup_layouts(editor_.GetText(), changes);
        try {
            preview_frame_ = request_preview(candidate, inspect_ui_markup(candidate), false);
        } catch (...) {
            preview_frame_.state["stale"] = true;
            throw;
        }
        for (const auto& change : changes) {
            const auto* element = preview_element(change.element);
            if (preview_frame_.state.value("stale", true) || !element ||
                !element->at("editable").get<bool>()) {
                preview_frame_.state["stale"] = true;
                throw AuthoringError("layout_unsupported",
                                     element ? element->at("reason").get<std::string>()
                                             : "Candidate has no editable preview geometry");
            }
        }
        replace_text(candidate);
        select_layouts(changes);
    }
    void arrange(std::span<const std::size_t> indices, std::string_view action, float dx, float dy,
                 const std::string& instance, std::uint64_t preview_revision) {
        check_preview_observation(instance, preview_revision);
        if (indices.size() < 2 || indices.size() > ui_source_selection_limit)
            throw AuthoringError("layout_selection", "Choose2..32 elements for group layout");
        const auto boxes = layout_boxes(indices);
        layouts(arrange_ui_markup_layouts(boxes, action, dx, dy), instance, preview_revision);
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
    UiMarkupControlProperties control_properties(std::size_t index) const {
        if (locator_.extension() != ".rml")
            throw AuthoringError("source_scope", "Open an RML document first");
        return read_ui_markup_control_properties(editor_.GetText(), outline_, index);
    }
    void control_values(std::size_t index,
                        const std::map<std::string, UiControlPropertyValue>& values) {
        if (locator_.extension() != ".rml")
            throw AuthoringError("source_scope", "Open an RML document first");
        replace_selected(set_ui_markup_control_properties(editor_.GetText(), index, values),
                         {index});
    }
    void attribute(std::size_t index, std::string_view name, std::optional<std::string> value) {
        replace_text(set_ui_markup_attribute(editor_.GetText(), index, name, std::move(value)));
    }
    std::optional<std::string> read_style(std::size_t index, std::string_view property) const {
        return read_ui_markup_style(editor_.GetText(), outline_, index, property);
    }
    std::optional<std::string> read_rule_property(std::size_t index,
                                                  std::string_view property) const {
        if (locator_.extension() != ".rcss")
            throw AuthoringError("source_scope", "Open an RCSS stylesheet first");
        return read_ui_stylesheet_property(editor_.GetText(), rules_, index, property);
    }
    void rule_property(std::size_t index, std::string_view property,
                       std::optional<std::string> value) {
        if (locator_.extension() != ".rcss")
            throw AuthoringError("source_scope", "Open an RCSS stylesheet first");
        const auto before = revision_;
        replace_text(
            set_ui_stylesheet_property(editor_.GetText(), index, property, std::move(value)));
        if (revision_ != before)
            select_rule(index);
    }
    void rule_selector(std::size_t index, std::string_view selector) {
        if (locator_.extension() != ".rcss")
            throw AuthoringError("source_scope", "Open an RCSS stylesheet first");
        const auto before = revision_;
        replace_text(set_ui_stylesheet_selector(editor_.GetText(), index, selector));
        if (revision_ != before)
            select_rule(index);
    }
    void add_rule(std::string_view selector) {
        if (locator_.extension() != ".rcss")
            throw AuthoringError("source_scope", "Open an RCSS stylesheet first");
        replace_text(insert_ui_stylesheet_rule(editor_.GetText(), selector));
        select_rule(rules_.size() - 1);
    }
    void remove_rule(std::size_t index) {
        if (locator_.extension() != ".rcss")
            throw AuthoringError("source_scope", "Open an RCSS stylesheet first");
        replace_text(remove_ui_stylesheet_rule(editor_.GetText(), index));
    }
    void copy_rule(std::size_t index) {
        if (locator_.extension() != ".rcss")
            throw AuthoringError("source_scope", "Open an RCSS stylesheet first");
        replace_text(duplicate_ui_stylesheet_rule(editor_.GetText(), index));
        select_rule(index + 1);
    }
    void move_rule(std::size_t index, int direction) {
        if (locator_.extension() != ".rcss")
            throw AuthoringError("source_scope", "Open an RCSS stylesheet first");
        const auto before = revision_;
        replace_text(move_ui_stylesheet_rule(editor_.GetText(), index, direction));
        if (before != revision_)
            select_rule(direction < 0 ? index - 1 : index + 1);
    }
    std::vector<std::optional<std::string>> read_styles(std::span<const std::size_t> indices,
                                                        std::string_view property) const {
        if (locator_.extension() != ".rml")
            throw AuthoringError("source_scope", "Open an RML document first");
        return read_ui_markup_styles(editor_.GetText(), outline_, indices, property);
    }
    std::vector<std::optional<std::string>>
    read_classes(std::span<const std::size_t> indices) const {
        if (locator_.extension() != ".rml")
            throw AuthoringError("source_scope", "Open an RML document first");
        return read_ui_markup_classes(editor_.GetText(), outline_, indices);
    }
    void styles(std::span<const std::size_t> indices, std::string_view property,
                std::optional<std::string> value) {
        if (locator_.extension() != ".rml")
            throw AuthoringError("source_scope", "Open an RML document first");
        replace_selected(
            set_ui_markup_styles(editor_.GetText(), indices, property, std::move(value)),
            std::vector<std::size_t>(indices.begin(), indices.end()));
    }
    void classes(std::span<const std::size_t> indices, std::optional<std::string> value) {
        if (locator_.extension() != ".rml")
            throw AuthoringError("source_scope", "Open an RML document first");
        replace_selected(set_ui_markup_classes(editor_.GetText(), indices, std::move(value)),
                         std::vector<std::size_t>(indices.begin(), indices.end()));
    }
    void style(std::size_t index, std::string_view property, std::optional<std::string> value) {
        const std::array indices{index};
        styles(indices, property, std::move(value));
    }
    void text(std::size_t index, std::string_view literal) {
        replace_text(set_ui_markup_text(editor_.GetText(), index, literal));
    }
    void insert(std::size_t parent, std::string_view tag, std::string_view literal,
                std::optional<std::size_t> before = {}) {
        replace_text(insert_ui_markup_child(editor_.GetText(), parent, tag, literal, before));
    }
    void control(std::size_t parent, std::string_view kind, std::string_view literal,
                 std::optional<std::size_t> before = {}) {
        const auto edit =
            insert_ui_markup_control(editor_.GetText(), parent, kind, literal, before);
        replace_text(edit.source);
        select(edit.element);
    }
    void remove(std::size_t index) {
        const std::array indices{index};
        removes(indices);
    }
    void removes(std::span<const std::size_t> indices) {
        replace_text(remove_ui_markup_elements(editor_.GetText(), indices));
    }
    void reparent(std::span<const std::size_t> indices, std::size_t parent,
                  std::optional<std::size_t> before = {}) {
        replace_text(reparent_ui_markup_elements(editor_.GetText(), indices, parent, before));
    }
    void duplicate(std::size_t index, std::string_view prefix = {}) {
        if (index >= outline_.size())
            throw AuthoringError("source_element", "Choose an element from the current UI outline");
        const auto insertion = outline_.at(index).source.end;
        const auto candidate = duplicate_ui_markup_element(editor_.GetText(), index, prefix);
        replace_text(candidate);
        for (std::size_t i = 0; i < outline_.size(); ++i)
            if (outline_[i].source.begin == insertion) {
                select(i);
                break;
            }
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
        if (gesture_active())
            throw AuthoringError("source_busy",
                                 "Finish or cancel the source gesture before saving");
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
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                             !ImGui::GetIO().AppFocusLost;
        if (locked || close_ || !contents || !focused ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ++drag_epoch_;
            outline_drag_active_ = false;
        }
        bool elements_visible = false;
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
                const bool stylesheet = locator_.extension() == ".rcss";
                const bool elements_tab = ImGui::BeginTabItem(stylesheet ? "Rules" : "Elements");
                FORGE_UI_PROBE("ui-source:elements-tab");
                if (elements_tab) {
                    elements_visible = !stylesheet;
                    gesture_.reset();
                    if (stylesheet)
                        rule_controls(locked);
                    else
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
            if (save_key && !gesture_active())
                attempt([&] { save(project); });
        }
        if (elements_active_ && !elements_visible)
            ++drag_epoch_;
        elements_active_ = elements_visible;
        outline_drag_active_ = false;
        if (elements_visible && !locked && focused && !close_)
            if (const auto* payload = ImGui::GetDragDropPayload();
                payload && payload->IsDataType(source_drag_type)) {
                try {
                    (void)drag_observation(*payload);
                    outline_drag_active_ = true;
                } catch (const std::exception&) {
                    // An invalidated native payload cannot authorize a source operation.
                }
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
