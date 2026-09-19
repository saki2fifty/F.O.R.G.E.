#pragma once
#include "editor_state.hpp"
#include "widgets.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <forge/ecs_tools.hpp>
#include <set>
namespace forge::ui {
class EcsWorkspace {
  public:
    explicit EcsWorkspace(WorldContext& context) : context_(context) {}
    void request_open() { open_ = true; }
    void request_close() { open_ = false; }
    void menu() {
        if (ImGui::BeginMenu("ECS")) {
            if (ImGui::MenuItem("World inspection", nullptr, open_))
                request_open();
            help("Inspect the authoring world's native entities, queries, statistics and alerts.");
            ImGui::EndMenu();
        }
        help("Advanced Flecs tools. These do not replace normal scene authoring commands.");
    }
    void draw(Scene& scene, Problems& problems) {
        if (!open_ && !tools_)
            return;
        try {
            if (!tools_)
                tools_ = std::make_unique<EcsTools>(context_);
            tools_->poll_rest(ImGui::GetIO().DeltaTime);
            if (ImGui::GetTime() >= next_sample_) {
                next_sample_ = ImGui::GetTime() + .5;
                tools_->sample(.5f);
                stats_ = tools_->statistics();
                std::set<std::string> active;
                for (const auto& alert : tools_->alerts()) {
                    const auto key = alert.at("key").get<std::string>();
                    active.insert(key);
                    problems.report({key,
                                     alert.at("severity"),
                                     alert.at("text"),
                                     alert.at("entity"),
                                     {},
                                     alert.at("property"),
                                     scene.asset_id()});
                }
                for (const auto& old : alert_keys_)
                    if (!active.contains(old))
                        problems.resolve(old);
                alert_keys_ = std::move(active);
            }
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        if (!open_)
            return;
        ImGui::SetNextWindowSize({720, 540}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("ECS World inspection", &open_)) {
            heading("Authoring world",
                    "Native ECS inspection. JSON is diagnostic data, not a scene save format.");
            ImGui::BeginDisabled(!tools_);
            if (ImGui::BeginTabBar("ecs-tabs")) {
                if (ImGui::BeginTabItem("Statistics")) {
                    help("Native Flecs statistics; diagnostic entities are included in counts.");
                    for (const auto& [key, value] : stats_.items())
                        if (key != "history") {
                            ImGui::Text("%s: %s", key.c_str(), value.dump().c_str());
                            help(key == "memory_bytes" ? "Estimated memory tracked by Flecs in "
                                                         "bytes; not total editor process memory."
                                 : key == "world_delta"
                                     ? "Latest native world progress delta in seconds. This "
                                       "authoring world is not the Play clock."
                                 : key == "stages" ? "Native ECS stage count, not a count of "
                                                     "operating-system threads."
                                 : key == "frame_count"
                                     ? "Native world progress frames. Diagnostic sampling does not "
                                       "advance this count."
                                     : "Native Flecs count for this authoring world, including "
                                       "internal and diagnostic objects.");
                        }
                    std::vector<float> counts;
                    for (const auto& sample : stats_.value("history", Json::array()))
                        counts.push_back(sample.at("entities"));
                    if (!counts.empty())
                        ImGui::PlotLines("Entity history", counts.data(), int(counts.size()), 0,
                                         nullptr, FLT_MAX, FLT_MAX, {0, 70});
                    help("Native statistics ring buffer, up to 60 tool samples. This is diagnostic "
                         "sampling time, not gameplay frames.");
                    ImGui::TextWrapped("Fixed simulation time is owned by the Play process. This "
                                       "panel does not advance gameplay.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Query")) {
                    help("Use the pinned Flecs query language; results are paged to 100 rows.");
                    ImGui::InputTextWithHint("##query", "Flecs query expression", query_.data(),
                                             query_.size());
                    help("Enter native component/relationship terms, variables, optional and "
                         "negated terms. Query errors appear below.");
                    if (ImGui::BeginCombo("Component term", "Insert a registered component")) {
                        for (const auto& type : context_.schema().at("components")) {
                            const auto name = type.at("id").get<std::string>();
                            const auto native = context_.world().lookup(name.c_str());
                            if (ImGui::Selectable(
                                    type.at("display_name").get<std::string>().c_str())) {
                                std::string expression = query_.data();
                                if (expression == "*")
                                    expression.clear();
                                if (!expression.empty())
                                    expression += ", ";
                                expression += "#" + std::to_string(native.id());
                                if (expression.size() < query_.size()) {
                                    std::copy(expression.begin(), expression.end(), query_.begin());
                                    query_[expression.size()] = 0;
                                }
                            }
                            help(type.at("description").get<std::string>().c_str());
                        }
                        ImGui::EndCombo();
                    }
                    help("Insert a native numeric component ID from this world. IDs are temporary; "
                         "literal dots in component names require a backslash in native query "
                         "text.");
                    if (button("Run query",
                               "Evaluate against this world without authoring mutations.")) {
                        offset_ = 0;
                        run_query();
                    }
                    ImGui::SameLine();
                    if (button("Next 100", "Read the next page from the current expression.")) {
                        offset_ += 100;
                        run_query();
                    }
                    ImGui::SameLine();
                    if (button("First page", "Return to the beginning of the result.")) {
                        offset_ = 0;
                        run_query();
                    }
                    ImGui::Text("Offset: %u", offset_);
                    help("Number of matching rows skipped before this page.");
                    ImGui::Text("%zu rows on this page", query_rows_.size());
                    help("Rows returned by native query serialization on the current page.");
                    if (button("Copy results", "Copy this page as native Flecs JSON."))
                        SDL_SetClipboardText(query_result_.c_str());
                    if (ImGui::BeginChild("query-entities", {0, 110}, ImGuiChildFlags_Borders)) {
                        for (const auto& row : query_rows_) {
                            if (!row.contains("id"))
                                continue;
                            const auto id = row.at("id").get<ecs_entity_t>();
                            const auto reference = context_.reference(id);
                            const auto label =
                                row.value("name", std::to_string(id)) + "##" + std::to_string(id);
                            if (ImGui::Selectable(label.c_str())) {
                                try {
                                    entity_result_ = tools_->entity(id).dump(2);
                                    if (reference && reference->scene == scene.asset_id() &&
                                        editor_context)
                                        editor_context->selection.select_entity(
                                            reference->entity.str());
                                } catch (const std::exception& e) {
                                    error_ = e.what();
                                }
                            }
                            help(reference ? "Select this authored entity and inspect its native "
                                             "JSON in Entity / JSON."
                                           : "Inspect native JSON in Entity / JSON. This is not an "
                                             "authored scene entity.");
                        }
                    }
                    ImGui::EndChild();
                    text_output(query_result_);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Metrics")) {
                    help("Native Flecs gauges, counters, integrated member values and component "
                         "counts. Diagnostic time is separate from the game clock.");
                    if (ImGui::BeginCombo("Source component", metric_component_.empty()
                                                                  ? "Choose component"
                                                                  : metric_component_.c_str())) {
                        for (const auto& type : context_.schema().at("components")) {
                            const auto name = type.at("id").get<std::string>();
                            if (ImGui::Selectable(
                                    type.at("display_name").get<std::string>().c_str(),
                                    metric_component_ == name)) {
                                metric_component_ = name;
                                metric_field_.clear();
                            }
                            help(type.at("description").get<std::string>().c_str());
                        }
                        ImGui::EndCombo();
                    }
                    help("Select the component whose presence or numeric member is measured.");
                    if (ImGui::BeginCombo("Member", metric_field_.empty()
                                                        ? "Component presence"
                                                        : metric_field_.c_str())) {
                        if (ImGui::Selectable("Component presence", metric_field_.empty()))
                            metric_field_.clear();
                        help("Count matching entities, or measure time with the component using "
                             "Counter.");
                        for (const auto& type : context_.schema().at("components"))
                            if (type.at("id") == metric_component_)
                                for (const auto& field : type.at("fields")) {
                                    const auto storage = field.at("type");
                                    if (storage != "float32" && storage != "float64")
                                        continue;
                                    const auto name = field.at("id").get<std::string>();
                                    if (ImGui::Selectable(
                                            field.at("display_name").get<std::string>().c_str(),
                                            metric_field_ == name))
                                        metric_field_ = name;
                                    help(field.at("description").get<std::string>().c_str());
                                }
                        ImGui::EndCombo();
                    }
                    help("Member metrics read registered numeric Meta fields. Count uses component "
                         "presence.");
                    const char* kinds[] = {"Gauge", "Counter", "Counter increment", "Entity count"};
                    ImGui::Combo("Kind", &metric_kind_, kinds, 4);
                    help("Gauge reads current values; Counter measures an existing counter or "
                         "presence duration; increment integrates member × diagnostic delta; count "
                         "totals matching entities.");
                    if (button("Create metric",
                               "Register a session-only native metric. Incompatible source/kind "
                               "combinations are rejected.")) {
                        try {
                            const auto type = context_.world().lookup(metric_component_.c_str());
                            const auto source =
                                metric_field_.empty() ? type : type.lookup(metric_field_.c_str());
                            const char* names[] = {"gauge", "counter", "increment", "count"};
                            tools_->create_metric(source, names[metric_kind_],
                                                  !metric_field_.empty());
                            error_.clear();
                        } catch (const std::exception& e) {
                            error_ = e.what();
                        }
                    }
                    if (ImGui::BeginChild("metric-values", {0, -40}, ImGuiChildFlags_Borders)) {
                        ecs_entity_t remove = 0;
                        for (const auto& value : (tools_ ? tools_->metrics() : Json::array())) {
                            ImGui::PushID(
                                std::to_string(value.at("id").get<ecs_entity_t>()).c_str());
                            ImGui::Text("%s: %.6g", value.at("label").get<std::string>().c_str(),
                                        value.at("value").get<double>());
                            help("Native metric instance value; source and instance IDs can be "
                                 "inspected with Query / Entity JSON.");
                            if (value.at("removable")) {
                                ImGui::SameLine();
                                if (button("Remove",
                                           "Remove this tool-owned metric and its instances."))
                                    remove = value.at("metric");
                            }
                            ImGui::PopID();
                        }
                        if (remove)
                            tools_->remove_metric(remove);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Entity / JSON")) {
                    help("Inspect the selected entity or export diagnostic world JSON to the "
                         "clipboard.");
                    if (button("Inspect selected",
                               "Read native Flecs JSON for the selected authored entity.")) {
                        try {
                            const auto entity =
                                editor_context ? scene.entity(editor_context->selection.entity())
                                               : flecs::entity{};
                            if (!entity)
                                throw std::runtime_error("Select an entity in Hierarchy first");
                            entity_result_ = tools_->entity(entity.id()).dump(2);
                            error_.clear();
                        } catch (const std::exception& e) {
                            error_ = e.what();
                        }
                    }
                    ImGui::SameLine();
                    if (button("Copy world JSON",
                               "Copy native debug JSON; this is not FORGE scene persistence.")) {
                        try {
                            const auto text = tools_->export_world();
                            SDL_SetClipboardText(text.c_str());
                            error_.clear();
                        } catch (const std::exception& e) {
                            error_ = e.what();
                        }
                    }
                    text_output(entity_result_);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("REST / Explorer")) {
                    help("Optional loopback-only inspection. Off by default; does not permit scene "
                         "edits.");
                    const unsigned active = tools_ ? tools_->rest_port() : 0;
                    ImGui::Text("%s", active ? "Running: read-only authoring world" : "Stopped");
                    help("Current loopback listener state. Closing this window does not stop an "
                         "active listener; use Stop REST.");
                    ImGui::BeginDisabled(active != 0);
                    ImGui::InputInt("Port", &port_);
                    help("Local TCP port, 1024..65535. This setting is not saved or enabled on "
                         "startup.");
                    if (button("Start REST", "Bind 127.0.0.1 only. Existing authoring validation "
                                             "remains the edit boundary.")) {
                        try {
                            tools_->start_rest(unsigned(port_));
                            error_.clear();
                        } catch (const std::exception& e) {
                            error_ = e.what();
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(active == 0);
                    if (button("Stop REST", "Close the listener immediately and release the port."))
                        tools_->stop_rest();
                    ImGui::SameLine();
                    if (button("Open Flecs Explorer",
                               "Open the optional, unpinned official browser client. Compatibility "
                               "with this Flecs revision is best-effort. "
                               "Browser local-network restrictions may require permission or a "
                               "locally hosted Explorer.")) {
                        const auto url = "https://www.flecs.dev/explorer/?host=127.0.0.1:" +
                                         std::to_string(active);
                        if (!SDL_OpenURL(url.c_str()))
                            error_ = SDL_GetError();
                    }
                    ImGui::EndDisabled();
                    ImGui::TextWrapped("Hosted Explorer can change independently of FORGE. Use "
                                       "Statistics, Query, Metrics, Entity / JSON and Alerts here "
                                       "for inspection built against FORGE's exact Flecs version.");
                    ImGui::TextWrapped(
                        "Explorer editing, script execution and command capture are disabled on "
                        "this authoring connection. Use normal FORGE commands for scene changes.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Alerts")) {
                    help("Native Flecs alerts also appear in Problems and clear when resolved.");
                    ImGui::TextWrapped("%zu active alerts. Sampling continues after this tool has "
                                       "first been opened.",
                                       alert_keys_.size());
                    help("Active native alert instances. Resolved conditions are removed from "
                         "Problems at the next diagnostic sample.");
                    ImGui::TextWrapped(
                        "Recommended ranges are advisory. Hard ranges are enforced by FORGE when "
                        "an edit is committed or a subsystem is realized.");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndDisabled();
            if (!error_.empty())
                field_error(error_.c_str());
        }
        ImGui::End();
    }

  private:
    WorldContext& context_;
    std::unique_ptr<EcsTools> tools_;
    bool open_ = false;
    double next_sample_ = 0;
    unsigned offset_ = 0;
    int port_ = 27750, metric_kind_ = 3;
    std::string metric_component_, metric_field_;
    Json stats_ = Json::object(), query_rows_ = Json::array();
    std::array<char, 8193> query_{'*', '\0'};
    std::string query_result_, entity_result_, error_;
    std::set<std::string> alert_keys_;
    void run_query() {
        try {
            const auto result = tools_->query(query_.data(), offset_);
            query_rows_ = result.value("results", Json::array());
            query_result_ = result.dump(2);
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    static void text_output(const std::string& text) {
        if (ImGui::BeginChild("output", {0, -40}, ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar))
            ImGui::TextUnformatted(text.c_str());
        ImGui::EndChild();
    }
};
} // namespace forge::ui
