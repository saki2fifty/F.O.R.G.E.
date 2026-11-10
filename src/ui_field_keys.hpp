#pragma once
#include <array>
#include <string_view>

namespace forge::ui_field {
enum class Key {
    Left,
    Right,
    SelectLeft,
    SelectRight,
    WordLeft,
    WordRight,
    SelectWordLeft,
    SelectWordRight,
    Home,
    End,
    Backspace,
    Delete,
    SelectAll,
    Accept,
    Next,
    Previous,
    Up,
    Down,
    SelectUp,
    SelectDown,
    PageUp,
    PageDown,
    SelectPageUp,
    SelectPageDown,
    SelectHome,
    SelectEnd,
    DocumentHome,
    DocumentEnd,
    SelectDocumentHome,
    SelectDocumentEnd,
    WordBackspace,
    WordDelete,
    Space,
    Escape
};
struct NamedKey {
    std::string_view name;
    Key key;
};
// Shared native dispatch and automation discovery, independent of backend headers.
inline constexpr std::array keys{NamedKey{"left", Key::Left},
                                 NamedKey{"right", Key::Right},
                                 NamedKey{"select_left", Key::SelectLeft},
                                 NamedKey{"select_right", Key::SelectRight},
                                 NamedKey{"word_left", Key::WordLeft},
                                 NamedKey{"word_right", Key::WordRight},
                                 NamedKey{"select_word_left", Key::SelectWordLeft},
                                 NamedKey{"select_word_right", Key::SelectWordRight},
                                 NamedKey{"home", Key::Home},
                                 NamedKey{"end", Key::End},
                                 NamedKey{"backspace", Key::Backspace},
                                 NamedKey{"delete", Key::Delete},
                                 NamedKey{"select_all", Key::SelectAll},
                                 NamedKey{"accept", Key::Accept},
                                 NamedKey{"next", Key::Next},
                                 NamedKey{"previous", Key::Previous},
                                 NamedKey{"up", Key::Up},
                                 NamedKey{"down", Key::Down},
                                 NamedKey{"select_up", Key::SelectUp},
                                 NamedKey{"select_down", Key::SelectDown},
                                 NamedKey{"page_up", Key::PageUp},
                                 NamedKey{"page_down", Key::PageDown},
                                 NamedKey{"select_page_up", Key::SelectPageUp},
                                 NamedKey{"select_page_down", Key::SelectPageDown},
                                 NamedKey{"select_home", Key::SelectHome},
                                 NamedKey{"select_end", Key::SelectEnd},
                                 NamedKey{"document_home", Key::DocumentHome},
                                 NamedKey{"document_end", Key::DocumentEnd},
                                 NamedKey{"select_document_home", Key::SelectDocumentHome},
                                 NamedKey{"select_document_end", Key::SelectDocumentEnd},
                                 NamedKey{"word_backspace", Key::WordBackspace},
                                 NamedKey{"word_delete", Key::WordDelete}};
} // namespace forge::ui_field

namespace forge::ui_field {
// Native non-text widgets share key identifiers; text editing keeps its own menu.
inline constexpr std::array widget_keys{NamedKey{"left", Key::Left},
                                        NamedKey{"right", Key::Right},
                                        NamedKey{"up", Key::Up},
                                        NamedKey{"down", Key::Down},
                                        NamedKey{"home", Key::Home},
                                        NamedKey{"end", Key::End},
                                        NamedKey{"page_up", Key::PageUp},
                                        NamedKey{"page_down", Key::PageDown},
                                        NamedKey{"space", Key::Space},
                                        NamedKey{"accept", Key::Accept},
                                        NamedKey{"escape", Key::Escape},
                                        NamedKey{"next", Key::Next},
                                        NamedKey{"previous", Key::Previous}};
inline constexpr std::array<std::string_view, 6> widget_kinds{"checkbox", "radio",        "select",
                                                              "button",   "input-button", "range"};
} // namespace forge::ui_field
