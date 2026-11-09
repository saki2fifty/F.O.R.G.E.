#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Rml {
class Element;
class ElementFormControl;
class ElementFormControlInput;
class ElementFormControlTextArea;
class ElementDocument;
} // namespace Rml
namespace forge {
// Call-local views of the existing native controls, never retained field state.
using UiTextField = std::variant<Rml::ElementFormControlInput*, Rml::ElementFormControlTextArea*>;
std::optional<UiTextField> ui_text_field(Rml::Element*);
Rml::ElementFormControl* ui_text_field_control(const UiTextField&);
std::string_view ui_text_field_kind(const UiTextField&);
bool ui_text_field_editable(const UiTextField&);
bool ui_text_field_id(std::string_view);
inline constexpr std::size_t ui_text_field_nodes = 4096, ui_text_field_count = 64;
struct UiTextFieldScan {
    std::vector<UiTextField> fields;
    std::map<std::string, unsigned> ids;
    std::size_t visited = 0;
    bool truncated = false;
};
UiTextFieldScan scan_ui_text_fields(Rml::ElementDocument&);
} // namespace forge
