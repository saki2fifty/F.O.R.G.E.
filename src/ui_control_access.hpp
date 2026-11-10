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
class ElementFormControlSelect;
class ElementProgress;
class ElementDocument;
} // namespace Rml
namespace forge {
// Call-local views of the existing native controls, never retained field state.
using UiTextField = std::variant<Rml::ElementFormControlInput*, Rml::ElementFormControlTextArea*>;
std::optional<UiTextField> ui_text_field(Rml::Element*);
Rml::ElementFormControl* ui_text_field_control(const UiTextField&);
std::string_view ui_text_field_kind(const UiTextField&);
bool ui_text_field_editable(const UiTextField&);
bool ui_control_id(std::string_view);
inline constexpr std::size_t ui_control_nodes = 4096, ui_control_count = 64;
inline constexpr std::size_t ui_form_option_text_nodes = 256, ui_form_options_page_limit = 32;
template <class Control> struct UiControlScan {
    std::vector<Control> fields;
    std::map<std::string, unsigned> ids;
    std::size_t visited = 0;
    bool truncated = false;
};
using UiTextFieldScan = UiControlScan<UiTextField>;
UiTextFieldScan scan_ui_text_fields(Rml::ElementDocument&);
using UiFormControl = std::variant<Rml::ElementFormControlInput*, Rml::ElementFormControlSelect*,
                                   Rml::ElementProgress*>;
std::optional<UiFormControl> ui_form_control(Rml::Element*);
Rml::Element* ui_form_control_element(const UiFormControl&);
std::string_view ui_form_control_kind(const UiFormControl&);
bool ui_form_control_editable(const UiFormControl&);
using UiFormControlScan = UiControlScan<UiFormControl>;
UiFormControlScan scan_ui_form_controls(Rml::ElementDocument&);
// Literal button elements and native input type=button, with an independent count.
std::optional<Rml::Element*> ui_button(Rml::Element*);
std::string_view ui_button_kind(Rml::Element*);
bool ui_control_focusable(Rml::Element*);
bool ui_control_tab_stop(Rml::Element*);
std::string_view ui_widget_kind(Rml::Element*);
bool ui_widget_keyboard_editable(Rml::Element*);
bool ui_button_pressable(Rml::Element*);
using UiButtonScan = UiControlScan<Rml::Element*>;
UiButtonScan scan_ui_buttons(Rml::ElementDocument&);
std::optional<Rml::ElementFormControlInput*> ui_range(Rml::Element*);
using UiRangeScan = UiControlScan<Rml::ElementFormControlInput*>;
UiRangeScan scan_ui_ranges(Rml::ElementDocument&);
struct UiRangeParameters {
    float minimum = 0, maximum = 100, step = 1;
    bool admitted = false, vertical = false;
};
UiRangeParameters ui_range_parameters(Rml::ElementFormControlInput*);
bool ui_range_editable(Rml::ElementFormControlInput*);
// Bounded native text leaves; no DOM serialization or nested control values.
std::optional<std::string> ui_form_option_text(Rml::Element&, std::size_t maximum_bytes);
} // namespace forge
