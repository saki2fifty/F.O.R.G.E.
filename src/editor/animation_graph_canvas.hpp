#pragma once
#include "animation_graph_canvas_model.hpp"
#include "state_graph_canvas.hpp"

namespace forge {
class AnimationGraphCanvas {
  public:
    bool draft_dirty() const { return canvas_.draft_dirty(); }
    void cancel() { canvas_.cancel(); }
    void reset() {
        canvas_.reset();
        model_.reset();
    }
    bool flush(const StateGraphCanvas::Edit& edit) { return canvas_.flush(adapt(edit)); }
    void draw(const Json& source, std::uint64_t revision, AssetId selected, bool locked,
              const StateGraphCanvas::Edit& edit, const std::function<void(AssetId)>& select) {
        model_.emplace(source);
        std::vector<StateGraphConnection> edges;
        for (const auto& row : model_->projected.at("states"))
            if (row.at("kind") != "clip") {
                const auto to = row.at("id").get<std::uint32_t>();
                unsigned port = 0;
                for (const auto* field : {"first", "second"}) {
                    const auto from = row.at(field).get<std::uint32_t>();
                    if (from)
                        edges.push_back({to * 2 + port, from, to,
                                         std::string("Input ") + std::to_string(port + 1), port});
                    ++port;
                }
            }
        canvas_.draw(
            model_->projected, revision, model_->handle(selected), locked, adapt(edit),
            [&](auto h) { select(model_->identity(h)); }, edges, {}, {}, 0, {}, 0, "states",
            "nodes", "Output", 280,
            [&](auto h) {
                return model_->projected.at("states").at(h - 1).at("kind") == "clip" ? 0u : 2u;
            },
            [](Json& d, auto from, auto to, unsigned port) {
                d["states"][to - 1][port == 0 ? "first" : "second"] = from;
            },
            [&](std::uint32_t h) {
                const auto kind =
                    model_->projected.at("states").at(h - 1).at("kind").get<std::string>();
                return kind == "clip" ? "Clip" : kind == "blend" ? "Blend" : "Additive";
            });
    }

  private:
    StateGraphCanvas canvas_{{"Animation pose canvas", "animation_pose_graph",
                              "forge.animation-state-layout", "Move animation node",
                              "Arrange animation nodes", "Connect animation nodes", false}};
    std::optional<ui::AnimationGraphCanvasModel> model_;
    StateGraphCanvas::Edit adapt(const StateGraphCanvas::Edit& edit) {
        return [this, edit](auto revision, auto label, const auto& mutation) {
            if (!model_)
                return false;
            const auto model = *model_;
            auto projected = model.projected;
            mutation(projected);
            return edit(revision, std::move(label),
                        [model, projected](Json& source) { model.apply(source, projected); });
        };
    }
};
} // namespace forge
