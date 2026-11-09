#include "ui_text_boundaries.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utf8proc.h>

namespace forge {
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
    if (selection.begin < 0 || selection.end < selection.begin || selection.end > boundaries.back())
        throw std::runtime_error("UI text selection is outside the native scalar range");
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
    const auto valid = [&](UiTextSelection value) {
        return value.begin >= 0 && value.begin <= value.end && value.end <= boundaries.back();
    };
    if (!valid(before) || !valid(after))
        throw std::runtime_error("UI text selection is outside the native scalar range");
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
    const auto ceiling = [&](int point) {
        return std::lower_bound(boundaries.begin(), boundaries.end(), point);
    };
    const auto floor = [&](int point) {
        const auto next = ceiling(point);
        return *next == point ? next : std::prev(next);
    };
    const int next_caret = key == UiTextKey::Left ? *floor(caret) : *ceiling(caret);
    const int next_anchor = anchor < next_caret   ? *floor(anchor)
                            : anchor > next_caret ? *ceiling(anchor)
                                                  : anchor;
    if (next_anchor == anchor && next_caret == caret)
        return {};
    return UiTextOrientedSelection{next_anchor, next_caret};
}
} // namespace forge
