#pragma once
#include "property_drawer.hpp"
#include "search.hpp"
#include <forge/material_graph.hpp>
#include <functional>
namespace forge {
// Canvas selection/view/drafts are editor state. MaterialGraphDocument remains
// the authored owner; all completed edits enter its existing history.
class MaterialGraphCanvas {
  public:
    using Json = nlohmann::json;
    using Edit = std::function<void(std::string, const std::function<void(Json&)>&)>;
    std::set<GraphNodeId> selection;
    std::function<void()> extract_function;
    std::function<Json(const Json&, const std::set<GraphNodeId>&)> copy_selection;
    std::function<std::string(GraphFunctionId)> function_label;
    bool function_scope = false;
    ImVec2 pan{30, 30};
    float zoom = 1;
    bool draft_dirty() const { return property_dirty_; }
    bool take_preview_change() { return std::exchange(preview_changed_, false); }
    void reset() {
        selection.clear();
        property_ = nullptr;
        property_dirty_ = false;
        linking_.reset();
        dragging_ = false;
        frame_ = true;
    }
    Json preview_graph(const Json& graph) const {
        auto next = graph;
        if (property_dirty_)
            for (auto& n : next["nodes"])
                if (n.at("id") == property_.at("id"))
                    n["data"] = property_.at("data");
        return next;
    }
    void flush(const Edit& edit) {
        if (!property_dirty_)
            return;
        const auto p = property_;
        edit("Edit graph node", [p](Json& graph) {
            for (auto& n : graph["nodes"])
                if (n.at("id") == p.at("id"))
                    n["data"] = p.at("data");
        });
        property_dirty_ = false;
    }
    void draw(const Json& graph, uint64_t revision, const Edit& edit, bool locked) {
        std::vector<std::pair<std::string, std::function<void(Json&)>>> actions;
        bool frame_after_edit = false;
        auto queue = [&](std::string label, std::function<void(Json&)> f) {
            actions.emplace_back(std::move(label), std::move(f));
        };
        ImGui::BeginDisabled(locked);
        if (ui::button("Add node", "Search values, texture sampling, math and surface nodes."))
            ImGui::OpenPopup("Add graph node");
        FORGE_UI_PROBE("graph:add-node");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (extract_function) {
            ImGui::BeginDisabled(locked || selection.empty());
            if (ui::button("Extract function",
                           "Turn selected math nodes into a reusable typed function. Leave Surface "
                           "Output and parameter/texture bindings outside the selection."))
                extract_function();
            FORGE_UI_PROBE("graph:extract-function");
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ui::button("Frame graph", "Fit all node positions inside the graph canvas."))
            frame_ = true;
        ImGui::SameLine();
        FORGE_UI_PROBE("graph:frame");
        ImGui::Text("View %.3g%%", double(zoom) * 100);
        ui::help("Wheel zooms this graph. Middle mouse pans. Global interface zoom remains "
                 "Ctrl+Plus/Minus.");
        if (open_add_) {
            ImGui::OpenPopup("Add graph node");
            open_add_ = false;
        }
        if (ImGui::BeginPopup("Add graph node")) {
            ImGui::InputText("Search nodes", search_, sizeof(search_));
            FORGE_UI_PROBE("graph:search");
            ui::help("Filter node names, categories and descriptions without case sensitivity.");
            const auto filter = search_key(search_);
            for (const auto& schema : material_graph_node_schemas()) {
                if (schema.key == "function" || schema.key == "function_input" ||
                    schema.key == "function_output" ||
                    (function_scope && (schema.key == "surface" || schema.key == "parameter" ||
                                        schema.key.starts_with("texture"))))
                    continue;
                auto label = schema.category + " / " + schema.label;
                if (!filter.empty() &&
                    search_key(label + " " + schema.help).find(filter) == std::string::npos)
                    continue;
                if (ImGui::Selectable(label.c_str())) {
                    double y = 30;
                    for (const auto& existing : graph.at("nodes"))
                        y = std::max(y, existing.at("position").at(1).get<double>() + 280);
                    const auto node = create_material_graph_node(schema.key, {30, y});
                    frame_after_edit = true;
                    queue("Add graph node", [node](Json& g) { g["nodes"].push_back(node); });
                    selection = {node.at("id").get<GraphNodeId>()};
                    ImGui::CloseCurrentPopup();
                }
                FORGE_UI_PROBE("graph:node-choice:" + schema.key);
                ui::help(schema.help.c_str());
            }
            ImGui::EndPopup();
        }
        const auto available = ImGui::GetContentRegionAvail();
        const float scale = ui::interface_scale;
        const float properties = std::min(270.f * scale, available.x * .32f);
        ImGui::BeginChild("Node canvas",
                          {std::max(100.f, available.x - properties - 8 * scale), available.y},
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const auto origin = ImGui::GetCursorScreenPos();
        const auto size = ImGui::GetContentRegionAvail();
        auto* draw = ImGui::GetWindowDrawList();
        const auto mouse = ImGui::GetIO().MousePos;
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (frame_ && !graph.at("nodes").empty()) {
            double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
            for (const auto& n : graph.at("nodes")) {
                const auto x = n.at("position")[0].get<double>(),
                           y = n.at("position")[1].get<double>();
                minx = std::min(minx, x);
                miny = std::min(miny, y);
                maxx = std::max(maxx, x + 250);
                std::vector<MaterialGraphPort> inputs, outputs;
                try {
                    inputs = material_graph_inputs(n);
                    outputs = material_graph_outputs(n);
                } catch (const std::exception&) {
                }
                maxy = std::max(maxy, y + 42 + 24 * std::max(inputs.size(), outputs.size()));
            }
            zoom = std::clamp(float(std::min(size.x / ((maxx - minx) * scale + 40),
                                             size.y / ((maxy - miny) * scale + 40))),
                              .000001f, 2.f);
            pan = {float(-minx * zoom * scale + 20), float(-miny * zoom * scale + 20)};
            frame_ = false;
        }
        if (hovered && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0) {
            const auto old = zoom;
            zoom = std::clamp(zoom * std::pow(1.15f, ImGui::GetIO().MouseWheel), .000001f, 2.f);
            pan = {mouse.x - origin.x - (mouse.x - origin.x - pan.x) * zoom / old,
                   mouse.y - origin.y - (mouse.y - origin.y - pan.y) * zoom / old};
        }
        if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            pan.x += ImGui::GetIO().MouseDelta.x;
            pan.y += ImGui::GetIO().MouseDelta.y;
        }
        auto screen = [&](const Json& node) {
            const auto id = node.at("id").get<GraphNodeId>();
            auto x = node.at("position")[0].get<double>(), y = node.at("position")[1].get<double>();
            if (drag_positions_.contains(id)) {
                x = drag_positions_.at(id)[0];
                y = drag_positions_.at(id)[1];
            }
            return ImVec2{origin.x + pan.x + float(x) * zoom * scale,
                          origin.y + pan.y + float(y) * zoom * scale};
        };
        struct Node {
            const Json* source;
            std::vector<MaterialGraphPort> inputs, outputs;
            ImVec2 position;
            float height;
        };
        std::map<GraphNodeId, Node> nodes;
        for (const auto& n : graph.at("nodes")) {
            Node node{&n, {}, {}, screen(n), 0};
            try {
                node.inputs = material_graph_inputs(n);
                node.outputs = material_graph_outputs(n);
            } catch (const std::exception&) {
            }
            node.height =
                (42 + 24 * std::max(node.inputs.size(), node.outputs.size())) * zoom * scale;
            nodes.emplace(n.at("id").get<GraphNodeId>(), std::move(node));
        }
        auto endpoint = [&](const Json& ref, bool input) {
            const auto found = nodes.find(ref.at("node").get<GraphNodeId>());
            if (found == nodes.end())
                return origin;
            const auto& n = found->second;
            const auto& ports = input ? n.inputs : n.outputs;
            const auto port = ref.at("port").get<std::string>();
            const auto at = std::find_if(ports.begin(), ports.end(),
                                         [&](const auto& p) { return p.key == port; });
            const float row = at == ports.end() ? 0 : float(at - ports.begin());
            return ImVec2{n.position.x + (input ? 0 : 240 * zoom * scale),
                          n.position.y + (44 + row * 24) * zoom * scale};
        };
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        for (const auto& e : graph.at("edges")) {
            const auto a = endpoint(e.at("from"), false), b = endpoint(e.at("to"), true);
            const auto delta = std::max(40.f, std::abs(a.x - b.x) * .5f);
            draw->AddBezierCubic(a, {a.x + delta, a.y}, {b.x - delta, b.y}, b,
                                 IM_COL32(115, 170, 195, 230), 2 * scale);
        }
        bool hit = false;
        for (const auto& [id, n] : nodes) {
            const auto p = n.position;
            const float width = 240 * zoom * scale;
            const ImVec2 end{p.x + width, p.y + n.height};
            if (p.x > origin.x + size.x || end.x < origin.x || p.y > origin.y + size.y ||
                end.y < origin.y)
                continue;
            draw->AddRectFilled(p, end, IM_COL32(28, 34, 43, 255), 5 * scale);
            draw->AddRectFilled(p, {end.x, p.y + 28 * zoom * scale},
                                selection.contains(id) ? IM_COL32(49, 88, 112, 255)
                                                       : IM_COL32(40, 52, 65, 255),
                                5 * scale);
            draw->AddRect(p, end,
                          selection.contains(id) ? IM_COL32(104, 192, 223, 255)
                                                 : IM_COL32(70, 82, 98, 255),
                          5 * scale, 0, selection.contains(id) ? 2 * scale : scale);
            std::string label = n.source->at("type").get<std::string>();
            const auto& schemas = material_graph_node_schemas();
            const auto schema = std::find_if(schemas.begin(), schemas.end(),
                                             [&](const auto& s) { return s.key == label; });
            if (schema != schemas.end())
                label = schema->label;
            else
                label = "Unavailable: " + label;
            if (n.source->at("type") == "function" && function_label) {
                try {
                    label =
                        function_label(n.source->at("data").at("function").get<GraphFunctionId>());
                } catch (const std::exception&) {
                    label = "Missing function";
                }
            }
            if ((n.source->at("type") == "parameter" ||
                 n.source->at("type").get<std::string>().starts_with("texture")) &&
                n.source->at("data").contains("label") &&
                n.source->at("data").at("label").is_string())
                label += ": " + n.source->at("data").at("label").get<std::string>();
            const ImVec4 header_clip{p.x + 5 * zoom * scale, p.y, end.x - 5 * zoom * scale,
                                     p.y + 28 * zoom * scale};
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom,
                          {p.x + 10 * zoom * scale, p.y + 6 * zoom * scale},
                          IM_COL32(230, 236, 244, 255), label.c_str(), nullptr, 0, &header_clip);
            ImGui::PushID(id.str().c_str());
            ImGui::SetCursorScreenPos(p);
            ImGui::InvisibleButton("Node header", {width, 28 * zoom * scale});
            FORGE_UI_PROBE("graph:node:" + id.str());
            if (ImGui::IsItemHovered()) {
                hit = true;
                ui::help(schema != schemas.end()
                             ? schema->help.c_str()
                             : "Node schema is unavailable. Its source remains preserved; "
                               "compilation reports the missing type.");
            }
            if (ImGui::IsItemClicked()) {
                if (ImGui::GetIO().KeyCtrl) {
                    if (selection.contains(id))
                        selection.erase(id);
                    else
                        selection.insert(id);
                } else if (!selection.contains(id))
                    selection = {id};
                if (!locked && selection.contains(id)) {
                    dragging_ = true;
                    drag_positions_.clear();
                    for (const auto& selected : selection)
                        if (nodes.contains(selected)) {
                            const auto& pos = nodes.at(selected).source->at("position");
                            drag_positions_[selected] = {pos[0].get<double>(),
                                                         pos[1].get<double>()};
                        }
                }
            }
            if (!locked && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                selection = {id};
                ImGui::OpenPopup("Node actions");
            }
            if (ImGui::BeginPopup("Node actions")) {
                if (ImGui::MenuItem("Copy"))
                    clipboard_ = copy(graph);
                if (ImGui::MenuItem("Duplicate")) {
                    const auto clip = copy(graph);
                    queue("Duplicate graph nodes",
                          [clip](Json& g) { paste_material_graph_selection(g, clip, {35, 35}); });
                }
                if (ImGui::MenuItem("Delete")) {
                    const auto ids = selection;
                    queue("Delete graph nodes", [ids](Json& g) { remove(g, ids); });
                    selection.clear();
                }
                ImGui::EndPopup();
            }
            auto ports = [&](const auto& list, bool input) {
                for (size_t i = 0; i < list.size(); ++i) {
                    const auto& port = list[i];
                    const ImVec2 center{p.x + (input ? 0 : width),
                                        p.y + (44 + i * 24) * zoom * scale};
                    draw->AddCircleFilled(center, 5 * zoom * scale, port_color(port.type));
                    const auto text = port.label;
                    const auto textwidth = ImGui::CalcTextSize(text.c_str()).x * zoom;
                    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom,
                                  {input ? center.x + 10 * zoom * scale
                                         : center.x - textwidth - 10 * zoom * scale,
                                   center.y - 7 * zoom * scale},
                                  IM_COL32(205, 216, 228, 255), text.c_str());
                    const float hit_radius = std::min(8 * scale, 10 * zoom * scale);
                    ImGui::SetCursorScreenPos({center.x - hit_radius, center.y - hit_radius});
                    ImGui::PushID(port.key.c_str());
                    ImGui::InvisibleButton(input ? "Input port" : "Output port",
                                           {2 * hit_radius, 2 * hit_radius});
                    FORGE_UI_PROBE(std::string(input ? "graph:input:" : "graph:output:") +
                                   id.str() + ":" + port.key);
                    if (ImGui::IsItemHovered()) {
                        hit = true;
                        ui::help(input ? "Drop a matching output here. Right-click clears this "
                                         "input connection. Types require explicit Convert nodes."
                                       : "Drag this output to a matching input. Several inputs may "
                                         "share one output.");
                    }
                    if (!locked && !input && ImGui::IsItemClicked())
                        linking_ = Link{id, port.key, port.type, center};
                    if (!locked && input && linking_ &&
                        ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                        const auto source = *linking_;
                        const auto target = id;
                        const auto key = port.key;
                        if (source.type == port.type && source.node != target)
                            queue("Connect graph ports", [source, target, key](Json& g) {
                                erase_json(g["edges"], [&](const Json& e) {
                                    return e.at("to").at("node").get<GraphNodeId>() == target &&
                                           e.at("to").at("port") == key;
                                });
                                g["edges"].push_back(
                                    {{"id", GraphEdgeId::generate()},
                                     {"from", {{"node", source.node}, {"port", source.port}}},
                                     {"to", {{"node", target}, {"port", key}}}});
                            });
                        else
                            error_ = "Ports must have matching types. Use Convert for a "
                                     "scalar/vector/color conversion.";
                        linking_.reset();
                    }
                    if (!locked && input && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        const auto target = id;
                        const auto key = port.key;
                        queue("Disconnect graph port", [target, key](Json& g) {
                            erase_json(g["edges"], [&](const Json& e) {
                                return e.at("to").at("node").get<GraphNodeId>() == target &&
                                       e.at("to").at("port") == key;
                            });
                        });
                    }
                    ImGui::PopID();
                }
            };
            ports(n.inputs, true);
            ports(n.outputs, false);
            ImGui::PopID();
        }
        if (linking_)
            draw->AddBezierCubic(
                linking_->position, {linking_->position.x + 60, linking_->position.y},
                {mouse.x - 60, mouse.y}, mouse, port_color(linking_->type), 2 * scale);
        if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            for (auto& [id, p] : drag_positions_) {
                (void)id;
                p[0] += ImGui::GetIO().MouseDelta.x / (zoom * scale);
                p[1] += ImGui::GetIO().MouseDelta.y / (zoom * scale);
            }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            linking_.reset();
            if (dragging_) {
                const auto positions = drag_positions_;
                queue("Move graph nodes", [positions](Json& g) {
                    for (auto& n : g["nodes"]) {
                        const auto id = n.at("id").get<GraphNodeId>();
                        if (positions.contains(id))
                            n["position"] = positions.at(id);
                    }
                });
                dragging_ = false;
                drag_positions_.clear();
            }
        }
        if (hovered && !hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            selection.clear();
        if (!locked && hovered && !hit && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            open_add_ = true;
        draw->PopClipRect();
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(size);
        const bool canvas_focus = ImGui::IsWindowFocused();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Node properties", {properties, available.y}, ImGuiChildFlags_Borders);
        properties_draw(
            graph, revision,
            [&](std::string label, const std::function<void(Json&)>& f) {
                queue(std::move(label), f);
            },
            locked);
        if (!error_.empty())
            ui::field_error(error_);
        ImGui::EndChild();
        if (!locked && canvas_focus && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                const auto ids = selection;
                queue("Delete graph nodes", [ids](Json& g) { remove(g, ids); });
                selection.clear();
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
                clipboard_ = copy(graph);
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) &&
                !clipboard_.is_null()) {
                const auto clip = clipboard_;
                queue("Paste graph nodes",
                      [clip](Json& g) { paste_material_graph_selection(g, clip, {40, 40}); });
            }
        }
        for (const auto& [label, f] : actions)
            edit(label, f);
        // Added nodes are committed after drawing; frame the new document on
        // the next frame instead of fitting the pre-edit graph.
        if (frame_after_edit)
            frame_ = true;
    }

  private:
    Json copy(const Json& graph) const {
        return copy_selection ? copy_selection(graph, selection)
                              : copy_material_graph_selection(graph, selection);
    }
    struct Link {
        GraphNodeId node;
        std::string port;
        MaterialParameterType type;
        ImVec2 position;
    };
    std::optional<Link> linking_;
    bool dragging_ = false, frame_ = true, property_dirty_ = false, preview_changed_ = false,
         open_add_ = false;
    std::map<GraphNodeId, std::array<double, 2>> drag_positions_;
    Json clipboard_, property_;
    uint64_t property_revision_ = 0;
    char search_[128]{};
    std::string error_;
    static ImU32 port_color(MaterialParameterType type) {
        if (type == MaterialParameterType::Scalar)
            return IM_COL32(170, 195, 211, 255);
        if (type == MaterialParameterType::LinearColor3 ||
            type == MaterialParameterType::LinearColor4)
            return IM_COL32(220, 191, 98, 255);
        return IM_COL32(111, 190, 157, 255);
    }
    template <class F> static void erase_json(Json& value, F predicate) {
        value.erase(std::remove_if(value.begin(), value.end(), predicate), value.end());
    }
    static void remove(Json& g, const std::set<GraphNodeId>& ids) {
        erase_json(g["edges"], [&](const Json& e) {
            return ids.contains(e.at("from").at("node").get<GraphNodeId>()) ||
                   ids.contains(e.at("to").at("node").get<GraphNodeId>());
        });
        erase_json(g["nodes"],
                   [&](const Json& n) { return ids.contains(n.at("id").get<GraphNodeId>()); });
    }
    void properties_draw(const Json& graph, uint64_t revision, const Edit& edit, bool locked) {
        ui::heading("Node properties", "Select a node to edit its typed source values. Property "
                                       "drags form one source Undo step.");
        if (selection.size() != 1) {
            flush(edit);
            ImGui::TextWrapped(
                "Select one node to edit its properties. Ctrl-click selects several nodes.");
            return;
        }
        const auto id = *selection.begin();
        const Json* selected = nullptr;
        for (const auto& n : graph.at("nodes"))
            if (n.at("id").get<GraphNodeId>() == id)
                selected = &n;
        if (!selected)
            return;
        if (property_.is_null() || property_.at("id") != selected->at("id") ||
            (!property_dirty_ && property_revision_ != revision)) {
            flush(edit);
            property_ = *selected;
            property_revision_ = revision;
        }
        auto& data = property_["data"];
        const auto kind = property_.at("type").get<std::string>();
        const auto& schemas = material_graph_node_schemas();
        const auto known = std::find_if(schemas.begin(), schemas.end(), [&](const auto& s) {
            return s.key == kind && property_.at("version") == s.version;
        });
        if (known == schemas.end()) {
            ImGui::TextWrapped("Unavailable schema: %s. Source is preserved.", kind.c_str());
            return;
        }
        if (kind == "function" || kind == "function_input" || kind == "function_output") {
            ImGui::TextWrapped("%s", data.value("label", kind).c_str());
            ui::help("Stable typed function interface. Edit the function interior from the "
                     "Functions list above; extraction freezes its input/output port types.");
            return;
        }
        ImGui::TextWrapped("%s", kind.c_str());
        ui::help("Stable node schema key. Missing schemas remain preserved and report a "
                 "compilation diagnostic.");
        try {
            (void)material_graph_inputs(property_);
            (void)material_graph_outputs(property_);
            if (kind == "constant" || kind == "parameter") {
                const auto outputs = material_graph_outputs(property_);
                const auto& values = data.at("value");
                if (!values.is_array() ||
                    values.size() != material_parameter_width(outputs.front().type))
                    throw std::runtime_error("Invalid value width");
                for (const auto& v : values)
                    if (!v.is_number() || !std::isfinite(v.get<float>()))
                        throw std::runtime_error("Invalid numeric value");
            }
            for (auto key : {"label", "key", "semantic", "from", "type"})
                if (data.contains(key) && !data.at(key).is_string())
                    throw std::runtime_error("Invalid text property");
            if (data.contains("uv_set") &&
                (!data.at("uv_set").is_number_unsigned() &&
                 (!data.at("uv_set").is_number_integer() || data.at("uv_set").get<int64_t>() < 0)))
                throw std::runtime_error("Invalid UV set");
        } catch (const std::exception& e) {
            ui::field_error(e.what());
            ImGui::BeginDisabled(locked);
            if (ui::button("Reset node configuration",
                           "Restore known node defaults; unknown fields remain preserved.")) {
                const auto fresh = create_material_graph_node(kind, {0, 0});
                for (auto it = fresh.at("data").begin(); it != fresh.at("data").end(); ++it) {
                    if (it.key() == "key" && data.contains("key") && data.at("key").is_string() &&
                        !data.at("key").get<std::string>().empty())
                        continue;
                    data[it.key()] = it.value();
                }
                property_dirty_ = true;
                flush(edit);
            }
            ImGui::EndDisabled();
            return;
        }
        ImGui::BeginDisabled(locked);
        bool changed = false;
        // Labels above full-width controls remain readable in narrow columns.
        auto control = [](const char* label) {
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            return std::string("##") + label;
        };
        static constexpr const char* types[]{"scalar",  "vector2", "vector3",
                                             "vector4", "color3",  "color4"};
        if (kind == "constant" || kind == "parameter" || kind == "add" || kind == "subtract" ||
            kind == "multiply" || kind == "combine" || kind == "minimum" || kind == "maximum" ||
            kind == "divide" || kind == "cosine" || kind == "absolute" || kind == "fraction" ||
            kind == "floor" || kind == "lerp" || kind == "saturate" || kind == "one_minus" ||
            kind == "sine" || kind == "convert") {
            auto current = data.value("type", std::string("scalar"));
            int index = 0;
            for (int i = 0; i < 6; ++i)
                if (current == types[i])
                    index = i;
            if (ImGui::BeginCombo(control("Value type").c_str(), types[index])) {
                for (int i = 0; i < 6; ++i) {
                    const bool selected = index == i;
                    if (ImGui::Selectable(types[i], selected)) {
                        index = i;
                        data["type"] = types[index];
                        changed = true;
                    }
                    FORGE_UI_PROBE("graph:node-type:" + std::string(types[i]));
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("graph:node-type");
            ui::help("Scalar, vector and linear-color ports are distinct. Existing connections may "
                     "require an explicit conversion after a type change.");
            if (kind == "convert") {
                auto from = data.value("from", std::string("vector4"));
                int input = 0;
                for (int i = 0; i < 6; ++i)
                    if (from == types[i])
                        input = i;
                if (ImGui::Combo(control("From type").c_str(), &input, types, 6)) {
                    data["from"] = types[input];
                    changed = true;
                }
                ui::help("Conversion broadcasts a scalar, drops trailing lanes or fills new lanes "
                         "with zero and alpha with one.");
            }
            if (kind == "constant" || kind == "parameter") {
                const auto width = index == 0                   ? 1
                                   : index == 1                 ? 2
                                   : (index == 2 || index == 4) ? 3
                                                                : 4;
                if (!data.contains("value") || data["value"].size() != size_t(width)) {
                    data["value"] = Json::array();
                    for (int i = 0; i < width; ++i)
                        data["value"].push_back(i == 3 ? 1.0 : 0.0);
                    changed = true;
                }
                float values[4]{};
                for (int i = 0; i < width; ++i)
                    values[i] = data["value"][i].get<float>();
                bool edited = false;
                if (index >= 4)
                    edited =
                        width == 3
                            ? ImGui::ColorEdit3(control("Value").c_str(), values,
                                                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)
                            : ImGui::ColorEdit4(control("Value").c_str(), values,
                                                ImGuiColorEditFlags_Float |
                                                    ImGuiColorEditFlags_HDR);
                else
                    edited = ImGui::DragScalarN(control("Value").c_str(), ImGuiDataType_Float,
                                                values, width, .01f);
                FORGE_UI_PROBE("graph:node-value");
                ui::help("A finite GPU value. Colors are linear; parameter values are defaults "
                         "that material instances may override.");
                if (edited) {
                    for (int i = 0; i < width; ++i)
                        data["value"][i] = values[i];
                    changed = true;
                }
            }
        }
        if (kind == "component") {
            const auto from = data.value("from", std::string("color4"));
            int input = 0;
            for (int i = 0; i < 6; ++i)
                if (from == types[i])
                    input = i;
            if (ImGui::Combo(control("Input type").c_str(), &input, types, 6)) {
                data["from"] = types[input];
                changed = true;
            }
            int lane = data.value("lane", 0);
            if (ImGui::InputInt("Lane (X/R=0, W/A=3)", &lane)) {
                data["lane"] = lane;
                changed = true;
            }
            ui::help("Select a lane that exists in the input. Use G=1 and B=2 for a "
                     "metallic/roughness map; W/A=3 reads alpha.");
        }
        if (kind == "parameter" || kind.starts_with("texture")) {
            const auto key = data.value("key", std::string{});
            ImGui::TextWrapped("Binding: %s", key.c_str());
            ui::help("Stable shader/material binding identity. Rename the display label without "
                     "changing instance overrides.");
            char label[512]{};
            const auto text = data.value("label", key);
            std::copy_n(text.data(), std::min(text.size(), sizeof(label) - 1), label);
            if (ImGui::InputText(control("Label").c_str(), label, sizeof(label))) {
                data["label"] = label;
                changed = true;
            }
            FORGE_UI_PROBE("graph:node-label");
            ui::help("Human-readable parameter name; does not rename its stable binding.");
        }
        if (kind.starts_with("texture")) {
            static constexpr const char* semantics[]{"color", "data", "normal"};
            const auto value = data.value("semantic", std::string("color"));
            int index = value == "normal" ? 2 : value == "data" ? 1 : 0;
            if (ImGui::Combo(control("Usage").c_str(), &index, semantics, 3)) {
                data["semantic"] = semantics[index];
                changed = true;
            }
            ui::help("Color selects the color texture variant; data and normal sampling use their "
                     "matching cooked variants. Assign the texture in the material's slot.");
        }
        if (kind == "uv" || kind == "texture2d" || kind == "texture2d_array") {
            unsigned uv = data.value("uv_set", 0u);
            if (ImGui::InputScalar(control("UV set").c_str(), ImGuiDataType_U32, &uv)) {
                data["uv_set"] = uv;
                changed = true;
            }
            ui::help(
                "Logical mesh UV-set number; the renderer resolves it to a dense varying slot.");
        }
        ImGui::EndDisabled();
        if (changed) {
            property_dirty_ = preview_changed_ = true;
        }
        if (property_dirty_ &&
            (!ImGui::IsAnyItemActive() || ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
            flush(edit);
    }
};
} // namespace forge
