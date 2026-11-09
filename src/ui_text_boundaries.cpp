#include "ui_text_boundaries.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utf8proc.h>

namespace forge {
namespace {
void check_selection(UiTextSelection value, int last) {
    if (value.begin < 0 || value.end < value.begin || value.end > last)
        throw std::runtime_error("UI text selection is outside the native scalar range");
}
std::optional<UiTextOrientedSelection> align_selection(const std::vector<int>& boundaries,
                                                       UiTextOrientedSelection value, UiTextKey key,
                                                       bool selecting) {
    const auto ceiling = [&](int point) {
        return std::lower_bound(boundaries.begin(), boundaries.end(), point);
    };
    const auto floor = [&](int point) {
        const auto next = ceiling(point);
        return *next == point ? next : std::prev(next);
    };
    const int caret = key == UiTextKey::Left ? *floor(value.caret) : *ceiling(value.caret);
    const int anchor = !selecting             ? caret
                       : value.anchor < caret ? *floor(value.anchor)
                       : value.anchor > caret ? *ceiling(value.anchor)
                                              : value.anchor;
    if (anchor == value.anchor && caret == value.caret)
        return {};
    return UiTextOrientedSelection{anchor, caret};
}
} // namespace
std::vector<int> ui_grapheme_boundaries(std::string_view text) {
    if (text.size() > ui_edit_text_bytes)
        throw std::runtime_error("UI text editing supports fields up to 64 KiB");
    std::vector<int> result{0};
    int characters = 0;
    utf8proc_int32_t previous = 0, state = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        utf8proc_int32_t point = 0;
        const auto bytes =
            utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
                             utf8proc_ssize_t(text.size() - offset), &point);
        if (bytes <= 0 || !utf8proc_codepoint_valid(point))
            throw std::runtime_error("UI text editing requires valid UTF-8");
        if (characters == ui_edit_text_characters)
            throw std::runtime_error("UI text editing supports 16384 scalar characters per field");
        if (characters && utf8proc_grapheme_break_stateful(previous, point, &state))
            result.push_back(characters);
        previous = point;
        ++characters;
        offset += std::size_t(bytes);
    }
    if (characters)
        result.push_back(characters);
    return result;
}
std::optional<UiTextSelection> ui_text_key_selection(std::string_view text,
                                                     UiTextSelection selection, UiTextKey key) {
    const auto boundaries = ui_grapheme_boundaries(text);
    check_selection(selection, boundaries.back());
    const auto ceiling = [&](int point) {
        return std::lower_bound(boundaries.begin(), boundaries.end(), point);
    };
    const auto floor = [&](int point) {
        const auto next = ceiling(point);
        return *next == point ? next : std::prev(next);
    };
    UiTextSelection next = selection;
    if (selection.begin != selection.end) {
        next = {*floor(selection.begin), *ceiling(selection.end)};
    } else {
        const int cursor = selection.begin;
        auto right = ceiling(cursor);
        const bool boundary = *right == cursor;
        switch (key) {
        case UiTextKey::Left:
        case UiTextKey::Backspace:
            if (!cursor)
                return {};
            next.begin = *std::prev(right);
            if (key == UiTextKey::Backspace && !boundary)
                next.end = *right;
            break;
        case UiTextKey::Right:
        case UiTextKey::Delete:
            if (cursor == boundaries.back())
                return {};
            if (key == UiTextKey::Delete && !boundary)
                next.begin = *std::prev(right);
            if (boundary)
                ++right;
            next.end = *right;
            break;
        }
    }
    return next == selection ? std::optional<UiTextSelection>{} : next;
}
std::optional<UiTextOrientedSelection> ui_text_shift_selection(std::string_view text,
                                                               UiTextSelection before,
                                                               UiTextSelection after,
                                                               UiTextKey key) {
    const auto boundaries = ui_grapheme_boundaries(text);
    check_selection(before, boundaries.back());
    check_selection(after, boundaries.back());
    int anchor = 0, caret = 0;
    if (key == UiTextKey::Left && after.end == before.end && after.begin == before.begin - 1) {
        anchor = before.end;
        caret = after.begin;
    } else if (key == UiTextKey::Left && after.begin == before.begin &&
               after.end == before.end - 1) {
        anchor = before.begin;
        caret = after.end;
    } else if (key == UiTextKey::Right && after.begin == before.begin &&
               after.end == before.end + 1) {
        anchor = before.begin;
        caret = after.end;
    } else if (key == UiTextKey::Right && after.end == before.end &&
               after.begin == before.begin + 1) {
        anchor = before.end;
        caret = after.begin;
    } else
        return {}; // No proven one-scalar native movement; retain its current selection.
    return align_selection(boundaries, {anchor, caret}, key, true);
}
std::optional<UiTextOrientedSelection> ui_text_word_selection(std::string_view text,
                                                              UiTextSelection before,
                                                              UiTextSelection after, UiTextKey key,
                                                              bool selecting) {
    const auto boundaries = ui_grapheme_boundaries(text);
    check_selection(before, boundaries.back());
    check_selection(after, boundaries.back());
    if (before == after || (key != UiTextKey::Left && key != UiTextKey::Right))
        return {};
    if (!selecting) {
        if (after.begin != after.end || (key == UiTextKey::Left && after.end > before.end) ||
            (key == UiTextKey::Right && after.begin < before.begin))
            return {};
        return align_selection(boundaries, {after.begin, after.end}, key, false);
    }
    int anchor = 0, caret = 0;
    if (key == UiTextKey::Left && after.end == before.end && after.begin < before.begin) {
        anchor = before.end;
        caret = after.begin;
    } else if (key == UiTextKey::Left && after.begin == before.begin && after.end < before.end) {
        anchor = before.begin;
        caret = after.end;
    } else if (key == UiTextKey::Left && after.end == before.begin && after.begin < before.begin) {
        anchor = before.begin;
        caret = after.begin;
    } else if (key == UiTextKey::Right && after.begin == before.begin && after.end > before.end) {
        anchor = before.begin;
        caret = after.end;
    } else if (key == UiTextKey::Right && after.end == before.end && after.begin > before.begin) {
        anchor = before.end;
        caret = after.begin;
    } else if (key == UiTextKey::Right && after.begin == before.end && after.end > before.end) {
        anchor = before.end;
        caret = after.end;
    } else
        return {}; // No preserved native anchor identifies this transition.
    return align_selection(boundaries, {anchor, caret}, key, true);
}
} // namespace forge
