#pragma once
#include "widgets.hpp"
namespace forge::ui {
enum class Icon {
    Add,
    Select,
    Move,
    Rotate,
    Scale,
    Snap,
    View,
    Save,
    Undo,
    Redo,
    Play,
    Pause,
    Step,
    Stop,
    More
};
// FORGE-owned vector marks: no font glyph assumptions or external icon dependency.
inline bool icon_button(const char* id, Icon icon, const char* description, bool active = false) {
    const float side = ImGui::GetFrameHeight();
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool clicked = ImGui::Button(id, {side, side});
    if (active)
        ImGui::PopStyleColor();
    help(description);
    auto* d = ImGui::GetWindowDrawList();
    auto p = ImGui::GetItemRectMin();
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    const float t = std::max(1.f, side / 16);
    auto pt = [&](float x, float y) { return ImVec2{p.x + side * x, p.y + side * y}; };
    auto line = [&](float x, float y, float a, float b) {
        d->AddLine(pt(x, y), pt(a, b), color, t);
    };
    auto arrow = [&](float x, float y, float a, float b) {
        line(x, y, a, b);
        const float dx = (a - x) * .25f, dy = (b - y) * .25f;
        line(a, b, a - dx - dy * .6f, b - dy + dx * .6f);
        line(a, b, a - dx + dy * .6f, b - dy - dx * .6f);
    };
    switch (icon) {
    case Icon::Add:
        line(.5f, .22f, .5f, .78f);
        line(.22f, .5f, .78f, .5f);
        break;
    case Icon::Select:
        d->AddTriangle(pt(.28f, .2f), pt(.7f, .58f), pt(.46f, .6f), color, t);
        line(.46f, .6f, .57f, .8f);
        break;
    case Icon::Move:
        arrow(.5f, .5f, .5f, .2f);
        arrow(.5f, .5f, .5f, .8f);
        arrow(.5f, .5f, .2f, .5f);
        arrow(.5f, .5f, .8f, .5f);
        break;
    case Icon::Rotate:
        d->PathArcTo(pt(.5f, .5f), side * .28f, .5f, 5.6f, 20);
        d->PathStroke(color, 0, t);
        arrow(.8f, .25f, .65f, .27f);
        break;
    case Icon::Scale:
        d->AddRect(pt(.22f, .52f), pt(.48f, .78f), color, 0, 0, t);
        arrow(.42f, .58f, .8f, .2f);
        break;
    case Icon::Snap:
        d->PathArcTo(pt(.5f, .5f), side * .27f, 0, 3.14159265f, 14);
        d->PathStroke(color, 0, t);
        line(.23f, .5f, .23f, .22f);
        line(.77f, .5f, .77f, .22f);
        line(.17f, .28f, .29f, .28f);
        line(.71f, .28f, .83f, .28f);
        break;
    case Icon::View:
        d->AddEllipse(pt(.5f, .5f), {side * .32f, side * .19f}, color, 0, 20, t);
        d->AddCircle(pt(.5f, .5f), side * .08f, color, 12, t);
        break;
    case Icon::Save:
        d->AddRect(pt(.24f, .2f), pt(.76f, .8f), color, 0, 0, t);
        d->AddRect(pt(.35f, .2f), pt(.65f, .42f), color, 0, 0, t);
        d->AddRect(pt(.35f, .57f), pt(.65f, .8f), color, 0, 0, t);
        break;
    case Icon::Undo:
    case Icon::Redo: {
        const bool back = icon == Icon::Undo;
        auto x = [&](float f) { return back ? f : 1 - f; };
        line(x(.75f), .72f, x(.75f), .38f);
        line(x(.75f), .38f, x(.25f), .38f);
        line(x(.25f), .38f, x(.42f), .22f);
        line(x(.25f), .38f, x(.42f), .54f);
        break;
    }
    case Icon::Play:
        d->AddTriangleFilled(pt(.32f, .22f), pt(.78f, .5f), pt(.32f, .78f), color);
        break;
    case Icon::Pause:
        line(.37f, .22f, .37f, .78f);
        line(.63f, .22f, .63f, .78f);
        break;
    case Icon::Step:
        d->AddTriangleFilled(pt(.24f, .25f), pt(.63f, .5f), pt(.24f, .75f), color);
        line(.74f, .22f, .74f, .78f);
        break;
    case Icon::Stop:
        d->AddRectFilled(pt(.26f, .26f), pt(.74f, .74f), color);
        break;
    case Icon::More:
        for (float x : {.25f, .5f, .75f})
            d->AddCircleFilled(pt(x, .5f), side * .055f, color, 8);
        break;
    }
    if (active)
        d->AddLine(pt(.15f, .93f), pt(.85f, .93f), ImGui::GetColorU32(ImGuiCol_CheckMark),
                   t * 1.5f);
    return clicked;
}
inline void toolbar_next() {
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetFrameHeight() <=
        ImGui::GetCurrentWindow()->WorkRect.Max.x)
        ImGui::SameLine();
}
inline void tool_separator() {
    if (ImGui::GetItemRectMax().x + 18 * interface_scale + ImGui::GetFrameHeight() >
        ImGui::GetCurrentWindow()->WorkRect.Max.x)
        return;
    ImGui::SameLine(0, 6 * interface_scale);
    auto p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y + 4 * interface_scale},
                                        {p.x, p.y + ImGui::GetFrameHeight() - 4 * interface_scale},
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy({1, ImGui::GetFrameHeight()});
    ImGui::SameLine(0, 6 * interface_scale);
}
// Compact label/value rows stack at narrow widths; IDs are provided separately by the caller.
inline void property_label_row(const char* label, const char* description = "") {
    const float width = ImGui::GetContentRegionAvail().x;
    const float label_width = std::min(110 * interface_scale, width * .38f);
    const bool side_by_side =
        width >= 240 * interface_scale && ImGui::CalcTextSize(label).x <= label_width;
    const float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    help(description);
    if (side_by_side)
        ImGui::SameLine(x + label_width);
    ImGui::SetNextItemWidth(-1);
}
} // namespace forge::ui
