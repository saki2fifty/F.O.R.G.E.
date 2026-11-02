#include "editor/navigation_behavior_editor.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& scratch, float scale) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const std::filesystem::path path = "Assets/Patrol.behavior.json";
    auto created = NavigationBehaviorDocument::create(project.writer_guard(), path, [](AssetId id) {
        auto s = NavigationBehaviorSource::create(id);
        const auto second = add_navigation_behavior_state(s.document);
        const auto third = add_navigation_behavior_state(s.document);
        connect_navigation_behavior_states(s.document, 1, second);
        connect_navigation_behavior_states(s.document, second, third);
        // Opaque state fields are not animation semantics in this source format.
        s.document["states"][0]["exit"] = {{"extension", true}};
        return s;
    });
    const auto original = created->source().document;
    created.reset();
    NavigationBehaviorEditor editor;
    editor.open(project, path);
    require(editor.document() && editor.dirty(), "Unpublished behavior did not open");
    const auto authored = scene.document();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {2400, 2600};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    std::string message;
    bool locked = false;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({2300, 2500}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::Render();
    };
    auto target = [&](const std::string& id) {
        const auto at = test::ui_targets.find(id);
        require(at != test::ui_targets.end(), "Missing behavior control " + id);
        const auto b = at->second;
        const ImVec2 center{(b.minimum.x + b.maximum.x) / 2, (b.minimum.y + b.maximum.y) / 2};
        require(b.enabled && center.x >= b.clip_minimum.x && center.x <= b.clip_maximum.x &&
                    center.y >= b.clip_minimum.y && center.y <= b.clip_maximum.y,
                "Disabled/clipped behavior control " + id);
        return center;
    };
    auto mouse = [&](ImVec2 p, bool down) {
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, down);
        frame();
    };
    auto click = [&](const std::string& id) {
        frame();
        const auto p = target(id);
        mouse(p, false);
        mouse(p, true);
        mouse(p, false);
        frame();
    };
    auto number = [&](const std::string& id, const char* text) {
        frame();
        const auto p = target(id);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        mouse(p, false);
        mouse(p, true);
        mouse(p, false);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(text);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    frame();
    frame();
    target("behavior_graph:node:1");
    target("behavior_graph:edge:4");
    click("behavior_graph:node:2");
    number("navigation_behavior:name", "Travel");
    require(editor.document()->source().compiled().state(2).name == "Travel",
            "Selected behavior name was not edited");
    editor.undo();
    frame();
    click("behavior_graph:edge:4");
    number("navigation_behavior:condition_value", "2.5");
    require(editor.document()->source().compiled().state(1).transitions.front().value == 2.5,
            "Edge selection did not expose condition value");
    const auto revision = editor.document()->revision();
    number("navigation_behavior:condition_value", "-2");
    require(editor.document()->revision() == revision &&
                editor.document()->source().compiled().state(1).transitions.front().value == 2.5,
            "Invalid condition partly committed");
    editor.undo();
    frame();
    require(editor.document()->source().document == original,
            "Behavior Undo changed opaque source data");
    click("behavior_graph:out:1");
    click("behavior_graph:in:1");
    const auto self = editor.document()->source().compiled().state(1).transitions.back();
    require(self.id == 6 && self.destination == 1, "Supported self transition was not allocated");
    editor.undo();
    frame();
    click("behavior_graph:out:1");
    click("behavior_graph:in:3");
    require(editor.document()->source().compiled().state(1).transitions.back().id == 7,
            "Undo recycled an allocated edge ID");
    click("behavior_graph:edge:7");
    click("button:Higher priority");
    require(editor.document()->source().compiled().state(1).transitions.front().id == 7,
            "Transition priority did not change");
    editor.undo();
    editor.undo();
    frame();
    click("behavior_graph:out:1");
    number("navigation_behavior:name", "Renamed");
    require(!test::ui_targets.at("behavior_graph:in:3").enabled,
            "Source edit retained a stale connection gesture");
    editor.undo();
    frame();
    const auto start = target("behavior_graph:node:2");
    const auto before_drag = editor.document()->revision();
    const auto before_drag_cook = editor.document()->source().cook();
    mouse(start, false);
    mouse(start, true);
    mouse({start.x + 50 * scale, start.y + 25 * scale}, true);
    require(editor.document()->revision() == before_drag && editor.can_undo(),
            "Drag committed before release");
    mouse({start.x + 50 * scale, start.y + 25 * scale}, false);
    require(editor.document()->revision() == before_drag + 1 &&
                editor.document()->source().cook() == before_drag_cook,
            "Layout gesture changed runtime behavior");
    editor.undo();
    frame();
    click("behavior_graph:arrange");
    const auto arranged = editor.document()->source().document;
    locked = true;
    frame();
    require(!test::ui_targets.at("behavior_graph:out:1").enabled, "Play lock allowed graph edits");
    locked = false;
    frame();
    require(scene.document() == authored, "Source editor changed Scene history");
    editor.request_save();
    frame();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    frame();
    require(!editor.dirty() && bool(editor.take_catalog()), "Behavior Save / Publish failed");
    editor.request_close();
    frame();
    require(!editor.is_open(), "Published behavior could not close");
    editor.open(project, path);
    frame();
    frame();
    require(editor.document()->source().document == arranged, "Behavior layout was not persisted");
    click("button:Add state");
    editor.request_close();
    frame();
    frame();
    click("button:Keep editing");
    require(editor.is_open() && editor.close_cancelled, "Pending close could not be cancelled");
    editor.request_close();
    frame();
    frame();
    click("button:Discard");
    require(!editor.is_open(), "Dirty behavior could not discard");
    editor.open(project, path);
    frame();
    frame();
    require(editor.document()->source().document == arranged, "Discard wrote its draft to disk");
    editor.request_close();
    frame();
    ImGui::DestroyContext();
}
void exercise_tree(const std::filesystem::path& scratch, float scale) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const std::filesystem::path path = "Assets/Tree.behavior.json";
    auto created = NavigationBehaviorDocument::create(project.writer_guard(), path, [](AssetId id) {
        auto value = NavigationBehaviorSource::create_tree(id);
        set_navigation_behavior_node_kind(value.document, 1, NavigationBehaviorTreeKind::Sequence);
        const auto wait =
            add_navigation_behavior_node(value.document, NavigationBehaviorTreeKind::Wait);
        const auto point =
            add_navigation_behavior_node(value.document, NavigationBehaviorTreeKind::MoveToPoint);
        connect_navigation_behavior_nodes(value.document, 1, wait);
        connect_navigation_behavior_nodes(value.document, 1, point);
        return value;
    });
    created.reset();
    NavigationBehaviorEditor editor;
    editor.open(project, path);
    const auto authored = scene.document();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {2400, 2600};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    bool locked = false;
    std::string message;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({2300, 2500}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::Render();
    };
    auto click = [&](const std::string& key) {
        frame();
        const auto box = test::ui_targets.at(key);
        require(box.enabled, "Disabled tree control: " + key);
        const ImVec2 p{(box.minimum.x + box.maximum.x) * .5f,
                       (box.minimum.y + box.maximum.y) * .5f};
        require(p.x >= box.clip_minimum.x && p.x <= box.clip_maximum.x &&
                    p.y >= box.clip_minimum.y && p.y <= box.clip_maximum.y,
                "Clipped tree control: " + key);
        io.AddMousePosEvent(p.x, p.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    frame();
    frame();
    require(test::ui_targets.contains("button:Frame nodes"),
            "Tree did not use the shared node canvas");
    click("behavior_graph:node:3");
    require(test::ui_targets.contains("navigation_behavior:tree_point") &&
                !test::ui_targets.at("navigation_behavior:tree_root").enabled,
            "Tree node properties or parented-root lock are incorrect");
    click("behavior_graph:node:1");
    const auto before = editor.document()->source().document;
    click("navigation_behavior:tree_earlier:3");
    require(editor.document()->source().document.at("nodes")[0].at("children") ==
                Json::array({3, 2}),
            "Tree child order did not change through source history");
    editor.undo();
    frame();
    require(editor.document()->source().document == before,
            "Tree order Undo did not restore source");
    click("navigation_behavior:tree_disconnect:3");
    require(editor.document()->source().document.at("nodes").size() == 3 &&
                editor.document()->source().document.at("nodes")[0].at("children") ==
                    Json::array({2}),
            "Disconnect deleted its draft child or retained the connection");
    editor.undo();
    frame();
    click("navigation_behavior:tree_add");
    click("navigation_behavior:tree_add:wait");
    const auto added = editor.document()->source().document;
    require(added.at("nodes").size() == 4 && added.at("nodes").back().at("id") == 4 &&
                added.at("next_id") == 5,
            "Actual Add node lost monotonic identity");
    editor.undo();
    frame();
    require(editor.document()->source().document.at("nodes").size() == 3 &&
                editor.document()->source().document.at("next_id") == 5,
            "Tree Undo recycled an ID");
    editor.redo();
    frame();
    require(editor.document()->source().document == added, "Tree Redo changed its node");
    click("behavior_graph:node:4");
    const auto held = editor.document()->source().document;
    locked = true;
    frame();
    frame();
    require(!test::ui_targets.at("navigation_behavior:tree_add").enabled &&
                !test::ui_targets.at("navigation_behavior:tree_kind").enabled &&
                editor.document()->source().document == held && scene.document() == authored,
            "Locked tree controls changed source or authored Scene");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
void capture_graph(const std::filesystem::path& gallery, const std::filesystem::path& output,
                   bool tree) {
    const auto root = output / ("Capture-" + AssetId::generate().str());
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    NavigationBehaviorEditor editor;
    editor.open(project, tree ? "Assets/Navigation/PatrolChaseTree.behavior.json"
                              : "Assets/Navigation/Patrol.behavior.json");
    require(editor.document() && !editor.dirty(),
            "Capture requires the published original Gallery graph");
    const auto source = editor.document()->source().document, authored = scene.document();
    test::ImGuiSoftwareCapture capture(1180, tree ? 1280 : 1040);
    ui::style(1);
    ui::EditorUiContext ui_context;
    ui::ContextScope scope(ui_context);
    test::observe_ui = true;
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    std::string message;
    ImVec2 edge{};
    for (unsigned frame = 0; frame < 7; ++frame) {
        if (frame == 2)
            io.AddMousePosEvent(edge.x, edge.y);
        if (frame == 3 || frame == 4)
            io.AddMouseButtonEvent(0, frame == 3);
        test::ui_targets.clear();
        editor.poll(project, message);
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1160, tree ? 1260.f : 1020.f}, ImGuiCond_Always);
        editor.draw(project, scene, false);
        capture.render();
        if (frame == 1) {
            const auto b =
                test::ui_targets.at(tree ? "behavior_graph:node:5" : "behavior_graph:edge:6");
            require(b.enabled, "Capture transition is disabled");
            edge = {(b.minimum.x + b.maximum.x) / 2, (b.minimum.y + b.maximum.y) / 2};
            require(edge.x >= b.clip_minimum.x && edge.x <= b.clip_maximum.x &&
                        edge.y >= b.clip_minimum.y && edge.y <= b.clip_maximum.y,
                    "Capture transition has no visible hit center");
        }
    }
    require(test::ui_targets.contains(tree ? "navigation_behavior:tree_kind"
                                           : "navigation_behavior:condition_value") &&
                editor.document()->source().document == source && scene.document() == authored &&
                !editor.dirty(),
            "Capture did not select the real pause transition or changed authored values");
    capture.save(output /
                 (tree ? "navigation-behavior-tree.png" : "navigation-behavior-graph.png"));
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && (std::string_view(argv[3]) == "--capture" ||
                          std::string_view(argv[3]) == "--capture-tree")) {
            capture_graph(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]),
                          std::string_view(argv[3]) == "--capture-tree");
            std::cout << "Authentic Gallery graph controls captured through real ImGui input\n";
            return 0;
        }
        require(argc == 2, "Need behavior UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            exercise(std::filesystem::absolute(argv[1]), scale);
            exercise_tree(std::filesystem::absolute(argv[1]), scale);
        }
        std::cout
            << "Behavior canvas/fields at100/150/200%: stable edges, priority, invalid rollback, "
               "opaque data, drag/history, locking, publication and close guards passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
