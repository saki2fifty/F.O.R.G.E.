#pragma once
#include "camera.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <cctype>
#include <forge/authoring.hpp>
#include <forge/geometry.hpp>
#include <functional>
namespace forge::ui {
struct PaletteEntry {
    std::string label, operation;
    Json arguments;
    bool available = true;
    std::string help;
};
inline bool command_matches(std::string text, std::string filter) {
    auto lower = [](unsigned char c) { return char(std::tolower(c)); };
    std::transform(text.begin(), text.end(), text.begin(), lower);
    std::transform(filter.begin(), filter.end(), filter.begin(), lower);
    return text.find(filter) != std::string::npos;
}
inline std::vector<PaletteEntry> palette_entries(const std::string& selected, float snap,
                                                 Float3 target, bool at_target) {
    std::vector<PaletteEntry> entries;
    const auto catalog = authoring_commands();
    auto add = [&](std::string label, std::string op, Json arguments, bool available = true) {
        std::string help;
        for (const auto& c : catalog)
            if (c.at("id") == op)
                help = c.at("description");
        entries.push_back(
            {std::move(label), std::move(op), std::move(arguments), available, std::move(help)});
    };
    const Json entity = {{"entity", selected}};
    for (unsigned kind = 0; kind < 4; ++kind) {
        const auto p = at_target ? target : Float3{0, kind == 3 ? 0.0f : 1.0f, 0};
        add(std::string("Create / ") + primitive_names[kind], "entity.create",
            {{"kind", kind}, {"position", {{"x", p[0]}, {"y", p[1]}, {"z", p[2]}}}});
    }
    add("Entity / Duplicate subtree", "entity.duplicate", entity, !selected.empty());
    add("Entity / Delete subtree", "entity.delete", entity, !selected.empty());
    add("Entity / Move to scene root", "entity.reparent", {{"entity", selected}, {"parent", ""}},
        !selected.empty());
    add("Transform / Reset all", "transform.reset", entity, !selected.empty());
    add("Transform / Reset rotation", "transform.rotation",
        {{"entity", selected}, {"value", {{"x", 0}, {"y", 0}, {"z", 0}}}}, !selected.empty());
    add("Transform / Reset scale", "transform.scale",
        {{"entity", selected}, {"value", {{"x", 1}, {"y", 1}, {"z", 1}}}}, !selected.empty());
    add("Transform / Place on ground", "transform.ground", entity, !selected.empty());
    add("Transform / Snap position", "transform.snap", {{"entity", selected}, {"step", snap}},
        !selected.empty());
    for (unsigned kind = 0; kind < 4; ++kind)
        add(std::string("Shape / ") + primitive_names[kind], "appearance.shape",
            {{"entity", selected}, {"kind", kind}}, !selected.empty());
    const char* colors[] = {"Red", "Orange", "Yellow", "Green", "Blue", "Gray"};
    const Float3 rgb[] = {{.8f, .15f, .1f}, {.9f, .4f, .1f},  {.9f, .8f, .15f},
                          {.2f, .7f, .3f},  {.15f, .4f, .9f}, {.5f, .5f, .5f}};
    for (unsigned i = 0; i < 6; ++i)
        add(std::string("Color / ") + colors[i], "appearance.color",
            {{"entity", selected},
             {"value", {{"r", rgb[i][0]}, {"g", rgb[i][1]}, {"b", rgb[i][2]}}}},
            !selected.empty());
    entries.push_back(
        {"History / Undo", "history.undo", {}, true, "Undo one committed authored edit."});
    entries.push_back({"History / Redo", "history.redo", {}, true, "Redo one authored edit."});
    entries.push_back({"Inspect / Scene diagnostics",
                       "diagnostics",
                       {},
                       true,
                       "Open scene statistics and informational findings."});
    entries.push_back(
        {"Inspect / Component schema",
         "schema",
         {},
         true,
         "Inspect supported reflected property identities, defaults and constraints."});
    return entries;
}
class CommandWorkspace {
  public:
    bool diagnostics_open = false, schema_open = false;
    void menu() {
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Command palette", "Ctrl+Shift+P"))
                request_open_ = true;
            help("Search actions by name, navigate with arrow keys, then press Enter. Escape "
                 "closes.");
            ImGui::MenuItem("Scene diagnostics", nullptr, &diagnostics_open);
            help("Inspect scene counts and unsupported component data. Findings do not modify the "
                 "scene.");
            ImGui::MenuItem("Component schema", nullptr, &schema_open);
            help("View the built-in Flecs reflected schema used by authoring commands.");
            ImGui::EndMenu();
        }
        help("Authoring command search, scene diagnostics, and reflected component information.");
    }
    void draw(Scene& scene, std::string& selected, std::string& message, float snap, Float3 target,
              bool at_target, bool busy) {
        if (!busy && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr,
                                ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
            ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
            ImGui::IsKeyPressed(ImGuiKey_P, false))
            request_open_ = true;
        if (request_open_) {
            ImGui::OpenPopup("Command palette");
            filter_[0] = 0;
            index_ = 0;
            focus_ = true;
            request_open_ = false;
        }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize({std::min(640 * interface_scale, viewport->WorkSize.x * .9f),
                                  std::min(460 * interface_scale, viewport->WorkSize.y * .85f)},
                                 ImGuiCond_Appearing);
        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * .5f,
                                 viewport->WorkPos.y + viewport->WorkSize.y * .3f},
                                ImGuiCond_Appearing, {.5f, .3f});
        bool open = true;
        if (ImGui::BeginPopupModal("Command palette", &open, ImGuiWindowFlags_NoSavedSettings)) {
            heading("Find an action", "Search command names. Unavailable actions are disabled; "
                                      "selection and scene state determine availability.");
            if (focus_) {
                ImGui::SetKeyboardFocusHere();
                focus_ = false;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##command_filter",
                                         "Type create, color, transform, history...", filter_,
                                         sizeof(filter_)))
                index_ = 0;
            help("Filter commands. Up/Down selects a result, Enter runs it, Escape closes.");
            const auto authored = scene.document();
            bool found = false;
            for (const auto& entity : authored.at("entities"))
                found |= entity.at("id") == selected;
            if (!found)
                selected.clear();
            auto all = palette_entries(selected, snap, target, at_target);
            for (auto& entry : all) {
                if (entry.operation == "history.undo")
                    entry.available = scene.can_undo();
                if (entry.operation == "history.redo")
                    entry.available = scene.can_redo();
            }
            std::vector<PaletteEntry> matches;
            for (auto& entry : all)
                if (command_matches(entry.label, filter_))
                    matches.push_back(std::move(entry));
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                ++index_;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                --index_;
            index_ = std::clamp(index_, 0, std::max(0, int(matches.size()) - 1));
            const bool activate = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                ImGui::CloseCurrentPopup();
            if (busy)
                ImGui::TextWrapped("Finish the current operation or stop Play to edit.");
            ImGui::BeginChild("command_results", {0, 0});
            for (int i = 0; i < int(matches.size()); ++i) {
                const auto& e = matches[i];
                const bool inspect = e.operation == "diagnostics" || e.operation == "schema";
                const bool enabled = e.available && (!busy || inspect);
                ImGui::BeginDisabled(!enabled);
                const bool click = ImGui::Selectable(e.label.c_str(), i == index_);
                help(enabled ? e.help.c_str()
                             : "Select an entity and finish active editing or Play before running "
                               "this command.");
                if (i == index_ && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                                    ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
                    ImGui::SetScrollHereY();
                if (enabled && (click || (activate && i == index_))) {
                    try {
                        if (e.operation == "diagnostics")
                            diagnostics_open = true;
                        else if (e.operation == "schema")
                            schema_open = true;
                        else if (e.operation == "history.undo")
                            authoring_history(scene, false);
                        else if (e.operation == "history.redo")
                            authoring_history(scene, true);
                        else
                            selected = authoring_command(scene, e.operation, e.arguments)
                                           .at("selected")
                                           .get<std::string>();
                        message = e.label;
                        ImGui::CloseCurrentPopup();
                    } catch (const std::exception& error) {
                        message = error.what();
                    }
                }
                ImGui::EndDisabled();
            }
            if (matches.empty())
                ImGui::TextUnformatted("No matching commands.");
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        if (diagnostics_open)
            diagnostics(scene, selected);
        if (schema_open)
            schema(scene);
    }

  private:
    void diagnostics(const Scene& scene, std::string& selected) {
        ImGui::SetNextWindowSize({550 * interface_scale, 420 * interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene diagnostics", &diagnostics_open)) {
            heading("Scene health",
                    "Counts and informational findings for authored data. Unknown components are "
                    "preserved; this is not an asset or export validator.");
            if (revision_ != scene.revision()) {
                report_ = scene_diagnostics(scene);
                revision_ = scene.revision();
            }
            ImGui::Text("Entities %zu | Visible %zu | Prefabs %zu",
                        report_.at("entities").get<std::size_t>(),
                        report_.at("visible").get<std::size_t>(),
                        report_.at("prefabs").get<std::size_t>());
            help("Visible counts effective Position components on non-prefab entities, regardless "
                 "of camera visibility.");
            ImGui::Text("Owned components %zu | Revision %llu",
                        report_.at("owned_components").get<std::size_t>(),
                        static_cast<unsigned long long>(revision_));
            help("Owned components excludes inherited copies; revision changes on committed "
                 "document operations.");
            if (report_.at("items").empty())
                ImGui::TextUnformatted("No informational findings.");
            unsigned n = 0;
            for (const auto& item : report_.at("items")) {
                ImGui::PushID(int(n++));
                const auto label = item.at("entity").get<std::string>() + " / " +
                                   item.at("code").get<std::string>();
                if (ImGui::Selectable(label.c_str(),
                                      selected == item.at("entity").get_ref<const std::string&>()))
                    selected = item.at("entity");
                help(item.at("message").get_ref<const std::string&>().c_str());
                ImGui::TextWrapped("%s", item.at("message").get_ref<const std::string&>().c_str());
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
    void schema(const Scene& scene) {
        ImGui::SetNextWindowSize({600 * interface_scale, 480 * interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Component schema", &schema_open)) {
            heading("Reflected properties",
                    "Built-in Flecs fields, stable identifiers and constraints used by authoring "
                    "validation. Animatable metadata reserves compatible binding targets; "
                    "animation editing is not implemented yet.");
            const auto value = scene.schema();
            for (const auto& c : value.at("components")) {
                const auto id = c.at("id").get<std::string>();
                const bool expanded = ImGui::TreeNode(id.c_str());
                help("Expand to inspect registered property types, defaults and limits.");
                if (expanded) {
                    for (const auto& f : c.at("fields")) {
                        ImGui::BulletText("%s (%s)",
                                          f.at("property_id").get_ref<const std::string&>().c_str(),
                                          f.at("type").get_ref<const std::string&>().c_str());
                        help(f.at("description").get_ref<const std::string&>().c_str());
                        const auto defaults = "Default: " + f.at("default").dump() +
                                              " | Unit: " + f.at("unit").get<std::string>();
                        ImGui::TextWrapped("%s", defaults.c_str());
                        if (f.contains("minimum")) {
                            const auto range = "Range: " + f.at("minimum").dump() + " to " +
                                               f.at("maximum").dump();
                            ImGui::TextWrapped("%s", range.c_str());
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::End();
    }
    bool request_open_ = false, focus_ = false;
    char filter_[192]{};
    int index_ = 0;
    std::uint64_t revision_ = ~std::uint64_t{};
    Json report_;
};
} // namespace forge::ui
