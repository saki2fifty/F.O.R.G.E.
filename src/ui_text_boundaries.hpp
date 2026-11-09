#pragma once
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace forge {
inline constexpr std::size_t ui_edit_text_bytes = 64 * 1024;
inline constexpr int ui_edit_text_characters = 16384;
// Scalar offsets into the existing native value; no copied field or cursor owner.
struct UiTextSelection {
    int begin = 0, end = 0;
    bool operator==(const UiTextSelection&) const = default;
};
enum class UiTextKey { Left, Right, Backspace, Delete };
// Extended grapheme boundaries include zero and the complete scalar count.
// Admission of field/source contents remains with the caller.
std::vector<int> ui_grapheme_boundaries(std::string_view);
// A temporary native selection consumed by its ordinary key action.
// No result means that the existing native selection already has the desired behavior.
std::optional<UiTextSelection> ui_text_key_selection(std::string_view, UiTextSelection, UiTextKey);
// A call-local oriented range for the native selection API. Inputs are sorted
// observations before/after its Shift arrow, not another stored cursor/anchor.
struct UiTextOrientedSelection {
    int anchor = 0, caret = 0;
    bool operator==(const UiTextOrientedSelection&) const = default;
};
std::optional<UiTextOrientedSelection> ui_text_shift_selection(std::string_view, UiTextSelection,
                                                               UiTextSelection, UiTextKey);
// The native word policy chooses the destination. Only its admitted horizontal
// result is aligned; selecting identifies Ctrl+Shift rather than Ctrl alone.
std::optional<UiTextOrientedSelection> ui_text_word_selection(std::string_view, UiTextSelection,
                                                              UiTextSelection, UiTextKey,
                                                              bool selecting);
} // namespace forge
