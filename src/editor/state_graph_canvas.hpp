#pragma once
#include "../state_graph_layout.hpp"
#include "property_drawer.hpp"
#include <functional>
#include <map>
#include <span>
#include <vector>
namespace forge {
struct StateGraphConnection {
    std::uint32_t id, from, to;
    std::string label;
};
struct StateGraphCanvasOptions {
    std::string window = "Animation state canvas", probe = "animation_graph";
    std::string layout = "forge.animation-state-layout";
    std::string move = "Move animation state", arrange = "Arrange animation states";
    std::string connect = "Connect animation states";
    bool allow_self = false;
};
// Gesture/view state only. Source documents own values, identity and history.
class StateGraphCanvas {
  public:
    explicit StateGraphCanvas(StateGraphCanvasOptions options = {})
        : options_(std::move(options)) {}
    using Edit = std::function<bool(std::uint64_t, std::string, const std::function<void(Json&)>&)>;
    bool draft_dirty() const { return dragging_ && moved_; }
    void cancel() {
        dragging_ = moved_ = false;
        linking_ = 0;
    }
    void reset() {
        cancel();
        frame_ = true;
    }
    bool flush(const Edit& edit) {
        if (!draft_dirty()) {
            cancel();
            return true;
        }
        const auto id = dragged_;
        const auto position = pending_position_;
        const auto revision = revision_;
        cancel();
        const auto format = options_.layout;
        return edit(revision, options_.move, [=](Json& source) {
            asset_detail::set_state_graph_position(source, id, position, format);
        });
    }
    void draw(const Json& source, std::uint64_t revision, std::uint32_t selected, bool locked,
              const Edit& edit, const std::function<void(std::uint32_t)>& select,
              std::span<const StateGraphConnection> connections,
              const std::function<void(Json&, std::uint32_t, std::uint32_t)>& connect,
              const std::function<void(std::uint32_t)>& select_edge = {},
              std::uint32_t selected_edge = 0, const std::function<void()>& toolbar = {},
              std::uint32_t active = 0, std::string_view rows = "states",
              std::string_view noun = "states", std::string_view root_label = "Entry",
              float logical_height = 250) {
        if ((dragging_ && revision_ != revision) || (linking_ && link_revision_ != revision) ||
            locked || ImGui::GetIO().AppFocusLost ||
            ((dragging_ || linking_) &&
             !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            cancel();
        const auto frame_label = "Frame " + std::string(noun);
        if (ui::button(frame_label.c_str(), "Fit cards without editing the source."))
            frame_ = true;
        FORGE_UI_PROBE(options_.probe + ":frame");
        ImGui::SameLine();
        ImGui::BeginDisabled(locked);
        const auto arrange_label = "Arrange " + std::string(noun);
        if (ui::button(arrange_label.c_str(), "Arrange every card as one source Undo step.")) {
            edit(revision, options_.arrange, [&](Json& draft) {
                std::size_t ordinal = 0;
                for (const auto& row : source.at(rows)) {
                    asset_detail::set_state_graph_position(
                        draft, row.at("id").get<std::uint32_t>(),
                        {double(ordinal % 4) * 300, double(ordinal / 4) * 150}, options_.layout);
                    ++ordinal;
                }
            });
            frame_ = true;
        }
        FORGE_UI_PROBE(options_.probe + ":arrange");
        ImGui::EndDisabled();
        if (toolbar) {
            ImGui::SameLine();
            toolbar();
        }
        ImGui::TextDisabled("Select a card to edit it. Connect Out to In. Drag cards; Escape "
                            "cancels. Wheel zooms; middle mouse pans.");
        ImGui::BeginChild(options_.window.c_str(), {0, logical_height * ui::interface_scale},
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
        auto& io = ImGui::GetIO();
        const bool hovered = ImGui::IsWindowHovered();
        const float scale = ui::interface_scale;
        std::map<std::uint32_t, std::array<double, 2>> positions;
        std::size_t ordinal = 0;
        for (const auto& row : source.at(rows)) {
            const auto id = row.at("id").get<std::uint32_t>();
            positions[id] =
                dragging_ && id == dragged_
                    ? pending_position_
                    : asset_detail::state_graph_position(source, id, ordinal, options_.layout);
            ++ordinal;
        }
        if (frame_ && !positions.empty()) {
            double minx = 1000001, miny = 1000001, maxx = -1000001, maxy = -1000001;
            for (const auto& [id, p] : positions) {
                (void)id;
                minx = std::min(minx, p[0] - 70);
                miny = std::min(miny, p[1] - 45);
                maxx = std::max(maxx, p[0] + 310);
                maxy = std::max(maxy, p[1] + 85);
            }
            zoom_ = std::clamp(float(std::min((size.x - 32) / (scale * (maxx - minx)),
                                              (size.y - 32) / (scale * (maxy - miny)))),
                               .00001f, 1.f);
            pan_ = {16 - float(minx) * zoom_ * scale, 16 - float(miny) * zoom_ * scale};
            frame_ = false;
        }
        if (hovered && !dragging_ && io.MouseWheel != 0) {
            const auto old = zoom_;
            zoom_ = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), .00001f, 2.4f);
            pan_ = {io.MousePos.x - origin.x - (io.MousePos.x - origin.x - pan_.x) * zoom_ / old,
                    io.MousePos.y - origin.y - (io.MousePos.y - origin.y - pan_.y) * zoom_ / old};
        }
        if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            pan_.x += io.MouseDelta.x;
            pan_.y += io.MouseDelta.y;
        }
        const float factor = scale * zoom_;
        auto screen = [&](std::uint32_t id) {
            const auto& p = positions.at(id);
            return ImVec2{origin.x + pan_.x + float(p[0]) * factor,
                          origin.y + pan_.y + float(p[1]) * factor};
        };
        auto* draw = ImGui::GetWindowDrawList();
        auto probe_button = [&](const char* label, ImVec2 p, ImVec2 extent,
                                [[maybe_unused]] const std::string& probe) {
            ImGui::SetCursorScreenPos(p);
            const bool clicked = ImGui::InvisibleButton(label, extent);
            FORGE_UI_PROBE(probe);
            return clicked;
        };
        std::map<std::pair<std::uint32_t, std::uint32_t>, unsigned> repeated;
        for (const auto& edge : connections) {
            if (!positions.contains(edge.from) || !positions.contains(edge.to))
                continue;
            const auto a = screen(edge.from), b = screen(edge.to);
            const ImVec2 start{a.x + 240 * factor, a.y + 65 * factor}, end{b.x, b.y + 65 * factor};
            const auto ordinal = repeated[{edge.from, edge.to}]++;
            const float bend = (end.x < start.x ? -100 * factor : 0) - float(ordinal) * 24 * factor;
            const ImVec2 c1{start.x + 70 * factor, start.y + bend},
                c2{end.x - 70 * factor, end.y + bend};
            const bool chosen = selected_edge ? selected_edge == edge.id : edge.from == selected;
            draw->AddBezierCubic(start, c1, c2, end,
                                 chosen ? IM_COL32(240, 190, 60, 255) : IM_COL32(85, 175, 195, 255),
                                 2 * scale);
            draw->AddTriangleFilled(end, {end.x - 10 * factor, end.y - 5 * factor},
                                    {end.x - 10 * factor, end.y + 5 * factor},
                                    IM_COL32(85, 175, 195, 255));
            const ImVec2 middle{(start.x + 3 * c1.x + 3 * c2.x + end.x) / 8,
                                (start.y + 3 * c1.y + 3 * c2.y + end.y) / 8};
            draw->AddCircleFilled(middle, 4 * scale, IM_COL32(240, 190, 60, 255));
            ImGui::PushID(int(edge.id));
            if (probe_button("edge", {middle.x - 8 * scale, middle.y - 8 * scale},
                             {16 * scale, 16 * scale},
                             options_.probe + ":edge:" + std::to_string(edge.id))) {
                if (select_edge)
                    select_edge(edge.id);
                else
                    select(edge.from);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", edge.label.c_str());
            ImGui::PopID();
        }
        for (const auto& row : source.at(rows)) {
            const auto id = row.at("id").get<std::uint32_t>();
            const auto p = screen(id);
            ImGui::PushID(int(id));
            const ImVec2 extent{240 * factor, 85 * factor};
            draw->AddRectFilled(p, {p.x + extent.x, p.y + extent.y},
                                id == active     ? IM_COL32(32, 78, 55, 255)
                                : id == selected ? IM_COL32(43, 76, 95, 255)
                                                 : IM_COL32(28, 43, 53, 255),
                                4 * factor);
            draw->AddRect(p, {p.x + extent.x, p.y + extent.y},
                          id == selected ? IM_COL32(240, 190, 60, 255)
                                         : IM_COL32(90, 125, 145, 255),
                          4 * factor);
            const auto title = row.at("name").get<std::string>();
            const ImVec4 title_clip{p.x + 10 * factor, p.y + 5 * factor, p.x + 230 * factor,
                                    p.y + 36 * factor};
            draw->AddText(nullptr, ImGui::GetFontSize() * zoom_,
                          {p.x + 12 * factor, p.y + 10 * factor}, IM_COL32(225, 235, 240, 255),
                          title.c_str(), nullptr, 216 * factor, &title_clip);
            const auto status = (source.at("entry") == id ? std::string(root_label) + " | " : "") +
                                "ID " + std::to_string(id);
            draw->AddText(nullptr, ImGui::GetFontSize() * zoom_,
                          {p.x + 12 * factor, p.y + 38 * factor}, IM_COL32(140, 190, 210, 255),
                          status.c_str());
            if (probe_button("card", p, {extent.x, 55 * factor},
                             options_.probe + ":node:" + std::to_string(id)))
                select(id);
            if (!locked && ImGui::IsItemActivated()) {
                select(id);
                dragged_ = id;
                revision_ = revision;
                begin_mouse_ = io.MousePos;
                begin_position_ = positions.at(id);
                pending_position_ = begin_position_;
                dragging_ = true;
                moved_ = false;
            }
            ImGui::BeginDisabled(locked || !connect);
            if (probe_button("out", {p.x + extent.x - 18 * factor, p.y + 55 * factor},
                             {18 * factor, 25 * factor},
                             options_.probe + ":out:" + std::to_string(id))) {
                linking_ = id;
                link_revision_ = revision;
            }
            draw->AddCircleFilled({p.x + extent.x - 7 * factor, p.y + 65 * factor}, 4 * factor,
                                  IM_COL32(90, 210, 230, 255));
            ImGui::BeginDisabled(!linking_ || (!options_.allow_self && linking_ == id));
            if (probe_button("in", {p.x, p.y + 55 * factor}, {18 * factor, 25 * factor},
                             options_.probe + ":in:" + std::to_string(id))) {
                const auto from = linking_;
                linking_ = 0;
                if (edit(revision, options_.connect,
                         [=](Json& draft) { connect(draft, from, id); }))
                    select(from);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            draw->AddCircleFilled({p.x + 7 * factor, p.y + 65 * factor}, 4 * factor,
                                  IM_COL32(90, 210, 230, 255));
            draw->AddText(nullptr, ImGui::GetFontSize() * zoom_,
                          {p.x + 17 * factor, p.y + 56 * factor}, IM_COL32(140, 190, 210, 255),
                          "In");
            draw->AddText(nullptr, ImGui::GetFontSize() * zoom_,
                          {p.x + 193 * factor, p.y + 56 * factor}, IM_COL32(140, 190, 210, 255),
                          "Out");
            ImGui::PopID();
        }
        if (linking_ && positions.contains(linking_)) {
            const auto p = screen(linking_);
            draw->AddLine({p.x + 233 * factor, p.y + 65 * factor}, io.MousePos,
                          IM_COL32(240, 190, 60, 255), 2 * scale);
        }
        if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            pending_position_ = {
                std::clamp(begin_position_[0] + (io.MousePos.x - begin_mouse_.x) / factor,
                           -1000000., 1000000.),
                std::clamp(begin_position_[1] + (io.MousePos.y - begin_mouse_.y) / factor,
                           -1000000., 1000000.)};
            moved_ |= std::abs(io.MousePos.x - begin_mouse_.x) +
                          std::abs(io.MousePos.y - begin_mouse_.y) >
                      2;
        }
        if (dragging_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            flush(edit);
        ImGui::EndChild();
    }

  private:
    StateGraphCanvasOptions options_;
    ImVec2 pan_{16, 16}, begin_mouse_{};
    float zoom_ = 1;
    bool frame_ = true, dragging_ = false, moved_ = false;
    std::uint32_t dragged_ = 0, linking_ = 0;
    std::uint64_t revision_ = 0, link_revision_ = 0;
    std::array<double, 2> begin_position_{}, pending_position_{};
};
} // namespace forge
