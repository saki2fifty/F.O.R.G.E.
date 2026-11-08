#pragma once
#include "entity_ref_picker.hpp"
#include "widgets.hpp"
#include <forge/authoring.hpp>
#include <forge/sound_cue.hpp>
#include <optional>

namespace forge {
class SoundCueEditor {
    AssetId scene_{};
    std::string project_, entity_, error_;
    std::uint64_t revision_ = 0;
    Json base_, draft_;
    bool open_ = false;
    std::optional<std::size_t> reveal_;
    void reset(const Scene& scene, const Json& value) {
        base_ = draft_ = value;
        revision_ = scene.revision();
        error_.clear();
        reveal_.reset();
    }

  public:
    void draw(Scene& scene, const std::string& entity, const Json& value,
              const std::string& project) {
        ui::IdScope scope("sound-cue-editor");
        if (project_ != project || scene_ != scene.asset_id() || entity_ != entity) {
            if (open_ && draft_ != base_) {
                ImGui::TextWrapped("An unapplied Sound Cue draft belongs to another object. Return "
                                   "to that object or discard the draft.");
                const bool discard = ui::button(
                    "Discard other cue draft",
                    "Discard only the detached graph edits; authored values stay unchanged.");
                FORGE_UI_PROBE("sound-cue:discard-other");
                if (!discard)
                    return;
            }
            project_ = project;
            scene_ = scene.asset_id();
            entity_ = entity;
            open_ = false;
            reset(scene, value);
        }
        if (!open_) {
            ui::help("Compose Audio Source one-shots. Choices happen on each event; Ordered "
                     "selects the next child, without scheduling a playback sequence.");
            if (ui::button("Edit sound cue",
                           "Open a detached graph draft. Apply publishes one Undo step.")) {
                reset(scene, value);
                open_ = true;
            }
            FORGE_UI_PROBE("sound-cue:edit");
            return;
        }
        const bool stale = revision_ != scene.revision() || base_ != value;
        ImGui::TextWrapped("Graph draft — Apply changes the scene; Save scene writes it to disk.");
        bool enabled = draft_.at("enabled");
        if (ImGui::Checkbox("Enabled", &enabled))
            draft_["enabled"] = enabled;
        FORGE_UI_PROBE("sound-cue:enabled");
        auto number = [&](const char* label, [[maybe_unused]] const char* probe, Json& v) {
            auto n = v.get<std::uint32_t>();
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputScalar((std::string("##") + label).c_str(), ImGuiDataType_U32, &n))
                v = n;
            FORGE_UI_PROBE(probe);
        };
        number("Seed", "sound-cue:seed", draft_["seed"]);
        number("Root node", "sound-cue:root", draft_["root"]);
        const bool repeating = ImGui::TreeNodeEx(
            "Repeating events", (draft_.at("repeat_interval_seconds").get<double>() != 0 ||
                                 draft_.at("repeat_mode") == 1)
                                    ? ImGuiTreeNodeFlags_DefaultOpen
                                    : ImGuiTreeNodeFlags_None);
        FORGE_UI_PROBE("sound-cue:repeat-settings");
        if (repeating) {
            ui::help("Repeat on output time, after all Sources prepare. Pause/Stop repeating "
                     "controls future events; Audio Sources retain already requested voices.");
            int mode = draft_.at("repeat_mode");
            if (ImGui::Combo("Timing", &mode, "Interval\0After event finishes\0"))
                draft_["repeat_mode"] = mode;
            FORGE_UI_PROBE("sound-cue:repeat-mode");
            ImGui::TextUnformatted(mode == 1 ? "Gap after event (seconds)"
                                             : "Repeat interval (seconds; 0 off)");
            ImGui::SetNextItemWidth(-1);
            double interval = draft_.at("repeat_interval_seconds");
            if (ImGui::InputDouble("##repeat-interval", &interval, 0, 0, "%.6f"))
                draft_["repeat_interval_seconds"] = interval;
            FORGE_UI_PROBE("sound-cue:repeat-interval");
            ImGui::TextUnformatted(
                mode == 1 ? "Wait for this event's Source voices and tails, then 0–3600 seconds."
                          : "Use 0.25–3600 seconds to enable repetition.");
            number("Events per run (0 unlimited)", "sound-cue:repeat-limit",
                   draft_["repeat_event_limit"]);
            ui::help("Use 1–65535 scheduled events, including the first. A finite run finishes "
                     "after all its Source voices and source effects retire, including overlapping "
                     "interval events. Manual triggers and shared bus tails are separate.");
            bool startup = draft_.at("repeat_on_start");
            if (ImGui::Checkbox("Repeat on start", &startup))
                draft_["repeat_on_start"] = startup;
            FORGE_UI_PROBE("sound-cue:repeat-startup");
            bool pause = draft_.at("repeat_pause_with_game");
            if (ImGui::Checkbox("Pause repetition with game", &pause))
                draft_["repeat_pause_with_game"] = pause;
            FORGE_UI_PROBE("sound-cue:repeat-game-pause");
            ImGui::TreePop();
        }
        auto& nodes = draft_.at("nodes");
        static constexpr const char* kinds[]{"Source", "Layer", "Weighted choice",
                                             "Ordered choice"};
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            ui::IdScope node_scope(std::to_string(i).c_str());
            auto& node = nodes[i];
            const auto kind = node.at("kind").get<unsigned>();
            const auto title = "Node " + std::to_string(i) + " — " + kinds[std::min(3u, kind)];
            if (reveal_ == i) {
                ImGui::SetNextItemOpen(true);
                reveal_.reset();
            }
            const auto flags = i == draft_.at("root").get<std::uint32_t>()
                                   ? ImGuiTreeNodeFlags_DefaultOpen
                                   : ImGuiTreeNodeFlags_None;
            const bool expanded = ImGui::TreeNodeEx("node", flags, "%s", title.c_str());
            FORGE_UI_PROBE("sound-cue:node:" + std::to_string(i));
            if (!expanded)
                continue;
            int selected = int(kind);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##kind", &selected, kinds, 4)) {
                node["kind"] = selected;
                if (selected == 0)
                    node["children"] = Json::array();
                else {
                    node["source"] = nullptr;
                    node["delay_seconds"] = 0.;
                    for (auto& edge : node["children"])
                        if (selected != 2)
                            edge["weight"] = 1;
                }
            }
            FORGE_UI_PROBE("sound-cue:kind:" + std::to_string(i));
            if (node.at("kind") == 0) {
                ImGui::TextUnformatted("Audio Source");
                ImGui::SetNextItemWidth(-1);
                entity_ref_picker(scene, node["source"], "##AudioSource", false,
                                  scene.world().id<AudioSource>());
                FORGE_UI_PROBE("sound-cue:source:" + std::to_string(i));
                ImGui::TextUnformatted("Delay (seconds, 0–10)");
                ImGui::SetNextItemWidth(-1);
                double delay = node.at("delay_seconds");
                if (ImGui::InputDouble("##delay", &delay, 0, 0, "%.6f"))
                    node["delay_seconds"] = delay;
                FORGE_UI_PROBE("sound-cue:delay:" + std::to_string(i));
            } else {
                auto& edges = node.at("children");
                for (std::size_t j = 0; j < edges.size(); ++j) {
                    ui::IdScope edge_scope(std::to_string(j).c_str());
                    auto& edge = edges[j];
                    number(
                        "Child node",
                        ("sound-cue:target:" + std::to_string(i) + ":" + std::to_string(j)).c_str(),
                        edge["target"]);
                    if (node.at("kind") == 2)
                        number("Weight (1–65535)",
                               ("sound-cue:weight:" + std::to_string(i) + ":" + std::to_string(j))
                                   .c_str(),
                               edge["weight"]);
                    if (node.at("kind") == 3) {
                        const auto target = edge.at("target").get<std::uint32_t>();
                        if (target < nodes.size() && nodes[target].at("kind") == 0)
                            ImGui::TextWrapped(
                                "Choice %zu: %s", j + 1,
                                entity_ref_label(scene, nodes[target].at("source")).c_str());
                        int direction = 0;
                        ImGui::BeginDisabled(j == 0);
                        if (ui::button("Move up",
                                       "Select this branch earlier in the Ordered choice."))
                            direction = -1;
                        FORGE_UI_PROBE("sound-cue:up:" + std::to_string(i) + ":" +
                                       std::to_string(j));
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::BeginDisabled(j + 1 == edges.size());
                        if (ui::button("Move down",
                                       "Select this branch later in the Ordered choice."))
                            direction = 1;
                        FORGE_UI_PROBE("sound-cue:down:" + std::to_string(i) + ":" +
                                       std::to_string(j));
                        ImGui::EndDisabled();
                        if (direction) {
                            std::swap(edges[j], edges[std::size_t(std::ptrdiff_t(j) + direction)]);
                            break;
                        }
                    }
                    if (ui::button("Remove child", "Remove this edge from the detached draft.")) {
                        edges.erase(edges.begin() + std::ptrdiff_t(j));
                        break;
                    }
                    FORGE_UI_PROBE("sound-cue:remove-child:" + std::to_string(i) + ":" +
                                   std::to_string(j));
                }
                ImGui::BeginDisabled(edges.size() >= sound_cue_child_limit);
                if (ui::button("Add child", "Add an edge, then enter its zero-based target node."))
                    edges.push_back(
                        {{"target", i == 0 && nodes.size() > 1 ? 1 : 0}, {"weight", 1}});
                FORGE_UI_PROBE("sound-cue:add-child:" + std::to_string(i));
                ImGui::EndDisabled();
            }
            bool referenced = nodes.size() > 1 && draft_.at("root") == i;
            for (std::size_t n = 0; n < nodes.size(); ++n)
                if (n != i)
                    for (const auto& edge : nodes[n].at("children"))
                        referenced |= edge.at("target") == i;
            ImGui::BeginDisabled(referenced);
            const bool remove =
                ui::button("Remove node", "Disconnect incoming edges and choose another root "
                                          "before removing a referenced node.");
            FORGE_UI_PROBE("sound-cue:remove-node:" + std::to_string(i));
            ImGui::EndDisabled();
            ImGui::TreePop();
            if (remove) {
                nodes.erase(nodes.begin() + std::ptrdiff_t(i));
                auto root = draft_.at("root").get<std::uint32_t>();
                draft_["root"] = root > i ? root - 1 : root;
                for (auto& remaining : nodes)
                    for (auto& edge : remaining.at("children")) {
                        const auto target = edge.at("target").get<std::uint32_t>();
                        if (target > i)
                            edge["target"] = target - 1;
                    }
                break;
            }
        }
        ImGui::BeginDisabled(nodes.size() >= sound_cue_node_limit);
        if (ui::button("Add source node",
                       "Append a Source node, then assign an existing Audio Source.")) {
            reveal_ = nodes.size();
            nodes.push_back(sound_cue_json({true, 1, 0, {{}}}).at("nodes").at(0));
        }
        FORGE_UI_PROBE("sound-cue:add-node");
        ImGui::EndDisabled();
        bool valid = true;
        try {
            const auto graph = SoundCueProgram::compile(sound_cue_from_json(draft_));
            ImGui::Text("Maximum one-shots per trigger: %zu / 16", graph.maximum_outputs());
        } catch (const std::exception& e) {
            valid = false;
            ImGui::TextWrapped("Graph needs attention: %s", e.what());
        }
        if (stale)
            ImGui::TextWrapped("Scene changed. Reset this draft before applying.");
        ImGui::BeginDisabled(!valid || stale || draft_ == base_);
        if (ui::button(
                "Apply sound cue",
                "Validate and publish all graph fields through scene history in one Undo step.")) {
            try {
                Json values;
                for (const char* key :
                     {"enabled", "seed", "root", "nodes", "repeat_interval_seconds",
                      "repeat_on_start", "repeat_pause_with_game", "repeat_mode",
                      "repeat_event_limit"})
                    values[key] = draft_.at(key);
                apply_authoring(scene,
                                Json::array({{{"operation", "property.set_many"},
                                              {"arguments",
                                               {{"entity", entity},
                                                {"component", "forge.sound_cue"},
                                                {"values", values}}}}}),
                                revision_);
                reset(scene, draft_);
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        FORGE_UI_PROBE("sound-cue:apply");
        ImGui::EndDisabled();
        if (ui::button("Reset sound cue draft",
                       "Reload the current scene values without changing scene history."))
            reset(scene, value);
        FORGE_UI_PROBE("sound-cue:reset");
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    }
};
} // namespace forge
