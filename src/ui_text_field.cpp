#include "ui_text_field.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <algorithm>
#include <forge/ui_protocol.hpp>

namespace forge {
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
    const auto* control = ui_text_field_control(field);
    const auto* document = control->GetOwnerDocument();
    return document && document->IsVisible() && control->IsVisible(true) && !control->IsDisabled();
}
bool ui_text_field_id(std::string_view id) {
    if (id.empty() || id.size() > 256)
        return false;
    try {
        ui_protocol::validate_value(std::string(id));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
UiTextFieldScan scan_ui_text_fields(Rml::ElementDocument& document) {
    UiTextFieldScan result;
    std::vector<Rml::Element*> nodes{&document};
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto* element = nodes[index];
        const auto& id = element->GetId();
        if (ui_text_field_id(id))
            ++result.ids[id];
        if (auto field = ui_text_field(element)) {
            if (result.fields.size() < ui_text_field_count)
                result.fields.push_back(*field);
            else
                result.truncated = true;
        }
        const auto count = std::size_t(element->GetNumChildren());
        const auto admitted = std::min(count, ui_text_field_nodes - nodes.size());
        for (std::size_t child = 0; child < admitted; ++child)
            nodes.push_back(element->GetChild(int(child)));
        result.truncated |= admitted != count;
    }
    result.visited = nodes.size();
    return result;
}
} // namespace forge
