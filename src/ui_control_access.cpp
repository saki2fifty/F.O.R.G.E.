#include "ui_control_access.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Elements/ElementProgress.h>
#include <algorithm>
#include <forge/ui_protocol.hpp>
#include <type_traits>

namespace forge {
namespace {
bool visible_control(Rml::Element* control) {
    const auto* document = control->GetOwnerDocument();
    return document && document->IsVisible() && control->IsVisible(true);
}
bool editable_control(Rml::ElementFormControl* control) {
    return visible_control(control) && !control->IsDisabled();
}
} // namespace
std::optional<UiTextField> ui_text_field(Rml::Element* element) {
    if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element)) {
        const auto type =
            Rml::StringUtilities::ToLower(input->GetAttribute<Rml::String>("type", "text"));
        if (type == "text" || type == "password")
            return input;
    } else if (auto* area = rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(element)) {
        return area;
    }
    return {};
}
Rml::ElementFormControl* ui_text_field_control(const UiTextField& field) {
    return std::visit([](auto* value) -> Rml::ElementFormControl* { return value; }, field);
}
std::string_view ui_text_field_kind(const UiTextField& field) {
    if (std::holds_alternative<Rml::ElementFormControlTextArea*>(field))
        return "textarea";
    const auto type = Rml::StringUtilities::ToLower(
        ui_text_field_control(field)->GetAttribute<Rml::String>("type", "text"));
    return type == "password" ? "password" : "text";
}
bool ui_text_field_editable(const UiTextField& field) {
    return editable_control(ui_text_field_control(field));
}
bool ui_control_id(std::string_view id) {
    if (id.empty() || id.size() > 256)
        return false;
    try {
        ui_protocol::validate_value(std::string(id));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
namespace {
template <class Scan, class Read>
Scan scan_controls(Rml::ElementDocument& document, Read read, bool include_non_dom) {
    Scan result;
    std::vector<Rml::Element*> nodes{&document};
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto* element = nodes[index];
        const auto& id = element->GetId();
        if (ui_control_id(id))
            ++result.ids[id];
        if (auto field = read(element)) {
            if (result.fields.size() < ui_control_count)
                result.fields.push_back(*field);
            else
                result.truncated = true;
        }
        const auto count = std::size_t(element->GetNumChildren(include_non_dom));
        const auto admitted = std::min(count, ui_control_nodes - nodes.size());
        for (std::size_t child = 0; child < admitted; ++child)
            nodes.push_back(element->GetChild(int(child)));
        result.truncated |= admitted != count;
    }
    result.visited = nodes.size();
    return result;
}
} // namespace
UiTextFieldScan scan_ui_text_fields(Rml::ElementDocument& document) {
    return scan_controls<UiTextFieldScan>(document, ui_text_field, false);
}
std::optional<UiFormControl> ui_form_control(Rml::Element* element) {
    if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element)) {
        // Native non-text type selection is case-sensitive at the selected pin.
        const auto type = input->GetAttribute<Rml::String>("type", "text");
        if (type == "checkbox" || type == "radio")
            return input;
    } else if (auto* select = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(element))
        return select;
    else if (auto* progress = rmlui_dynamic_cast<Rml::ElementProgress*>(element))
        return progress;
    return {};
}
Rml::Element* ui_form_control_element(const UiFormControl& control) {
    return std::visit([](auto* value) -> Rml::Element* { return value; }, control);
}
std::string_view ui_form_control_kind(const UiFormControl& control) {
    if (std::holds_alternative<Rml::ElementFormControlSelect*>(control))
        return "select";
    if (std::holds_alternative<Rml::ElementProgress*>(control))
        return "progress";
    const auto type = ui_form_control_element(control)->GetAttribute<Rml::String>("type", "text");
    if (type == "checkbox")
        return "checkbox";
    return type == "radio" ? "radio" : "unsupported";
}
bool ui_form_control_editable(const UiFormControl& control) {
    if (!ui_form_control(ui_form_control_element(control)))
        return false;
    return std::visit(
        [](auto* value) {
            if constexpr (std::is_same_v<decltype(value), Rml::ElementProgress*>)
                return false; // Read-only native display, not an input control.
            else
                return editable_control(value);
        },
        control);
}
UiFormControlScan scan_ui_form_controls(Rml::ElementDocument& document) {
    // Native dropdown options live below non-DOM widget children after Update.
    // Preserve the text scanner's original DOM-only scope/count policy above.
    return scan_controls<UiFormControlScan>(document, ui_form_control, true);
}
std::optional<Rml::Element*> ui_button(Rml::Element* element) {
    if (!element)
        return {};
    if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element)) {
        const auto type = input->GetAttribute<Rml::String>("type", "text");
        if (type == "button")
            return element;
    } else if (element->GetTagName() == "button" &&
               !rmlui_dynamic_cast<Rml::ElementFormControl*>(element))
        return element;
    return {};
}
std::string_view ui_button_kind(Rml::Element* element) {
    if (!ui_button(element))
        return "unsupported";
    return rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element) ? "input-button" : "button";
}
bool ui_button_focusable(Rml::Element* element) {
    return element && element->GetComputedValues().focus() != Rml::Style::Focus::None;
}
bool ui_button_pressable(Rml::Element* element) {
    if (!ui_button(element) || !visible_control(element) || !ui_button_focusable(element))
        return false;
    const auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
    return !control || !control->IsDisabled();
}
UiButtonScan scan_ui_buttons(Rml::ElementDocument& document) {
    return scan_controls<UiButtonScan>(document, ui_button, true);
}
std::optional<std::string> ui_form_option_text(Rml::Element& option, std::size_t maximum_bytes) {
    if (maximum_bytes > ui_protocol::max_string)
        return {};
    std::vector<Rml::Element*> nodes{&option};
    std::string result;
    std::size_t visited = 0;
    while (!nodes.empty()) {
        auto* element = nodes.back();
        nodes.pop_back();
        ++visited;
        if (rmlui_dynamic_cast<Rml::ElementFormControl*>(element) || ui_form_control(element))
            return {};
        if (const auto* text = rmlui_dynamic_cast<Rml::ElementText*>(element)) {
            const auto& value = text->GetText();
            if (value.size() > maximum_bytes - result.size())
                return {};
            result += value;
        }
        const auto count = static_cast<std::size_t>(element->GetNumChildren());
        if (count > ui_form_option_text_nodes - visited - nodes.size())
            return {};
        for (std::size_t child = count; child-- > 0;)
            nodes.push_back(element->GetChild(static_cast<int>(child)));
    }
    try {
        ui_protocol::validate_value(result);
        return result;
    } catch (const std::exception&) {
        return {};
    }
}
} // namespace forge
