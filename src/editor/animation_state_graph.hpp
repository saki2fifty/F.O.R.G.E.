#pragma once
#include "state_graph_canvas.hpp"
namespace forge {
// Animation source semantics around the shared state-canvas gestures.
class AnimationStateGraph {
  public:
    using Edit = StateGraphCanvas::Edit;
    bool draft_dirty() const { return canvas_.draft_dirty(); }
    void cancel() { canvas_.cancel(); }
    void reset() { canvas_.reset(); }
    bool flush(const Edit& edit) { return canvas_.flush(edit); }
    void draw(const Json& source, std::uint64_t revision, std::uint32_t selected, bool locked,
              const Edit& edit, const std::function<void(std::uint32_t)>& select) {
        std::vector<StateGraphConnection> edges;
        if (source.at("version") == 2)
            for (const auto& row : source.at("states")) {
                if (!row.contains("exit") || row.at("exit").is_null())
                    continue;
                const auto id = row.at("id").get<std::uint32_t>();
                edges.push_back({id, id, row.at("exit").at("state").get<std::uint32_t>(),
                                 "Exit at " +
                                     std::to_string(row.at("exit").at("phase").get<double>()) +
                                     " cycles. Select to edit the outgoing state."});
            }
        auto connect = [](Json& draft, std::uint32_t from, std::uint32_t to) {
            draft["version"] = 2;
            for (auto& state : draft["states"])
                if (state.at("id") == from) {
                    auto exit = state.value("exit", Json{});
                    if (!exit.is_object())
                        exit = Json::object();
                    exit["state"] = to;
                    if (!exit.contains("phase"))
                        exit["phase"] = 1;
                    state["exit"] = std::move(exit);
                }
        };
        auto toolbar = [&] {
            const auto at = std::find_if(source.at("states").begin(), source.at("states").end(),
                                         [&](const auto& row) { return row.at("id") == selected; });
            const bool exit = source.at("version") == 2 && at != source.at("states").end() &&
                              at->contains("exit") && !at->at("exit").is_null();
            ImGui::BeginDisabled(locked || !exit);
            if (ui::button("Remove selected exit",
                           "Remove this timed connection as one source Undo step."))
                edit(revision, "Remove animation exit", [=](Json& draft) {
                    for (auto& row : draft["states"])
                        if (row.at("id") == selected)
                            row["exit"] = nullptr;
                });
            FORGE_UI_PROBE("animation_graph:remove-exit");
            ImGui::EndDisabled();
        };
        canvas_.draw(source, revision, selected, locked, edit, select, edges, connect, {}, 0,
                     toolbar);
    }

  private:
    StateGraphCanvas canvas_;
};
} // namespace forge
