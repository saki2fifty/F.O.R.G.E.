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
    Previous
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
                                 NamedKey{"previous", Key::Previous}};
} // namespace forge::ui_field
