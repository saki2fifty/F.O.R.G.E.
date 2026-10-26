#include "component_inspector.hpp"
#include "runtime_tuning_review.hpp"
#include <forge/navigation.hpp>
#include <forge/runtime.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(float scale, ImVec2 size) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Agent"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_agent"}});
    const auto saved = scene.document();
    const auto schema = scene.schema();
    auto effective = scene.effective_document();
    Json* entity = nullptr;
    for (auto& e : effective.at("entities"))
        if (e.at("id") == actor)
            entity = &e;
    check(entity != nullptr, "Missing agent fixture");
    check(ComponentInspector::has_runtime_fields(effective, schema, actor),
          "Navigation Agent configuration did not expose its tunable runtime fields");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    unsigned submissions = 0;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Inspector");
        inspector.draw_runtime(
            effective, schema, {}, actor,
            [&](const auto&, const auto&, const auto&) {
                ++submissions;
                return true;
            },
            false);
        ImGui::End();
        ImGui::Render();
    };
    for (const char* phase :
         {"Pending", "Moving", "Arrived", "Paused", "Blocked", "Failed", "Idle", "Disabled"}) {
        const bool sampled =
            std::string_view(phase) == "Moving" || std::string_view(phase) == "Arrived" ||
            std::string_view(phase) == "Paused" || std::string_view(phase) == "Blocked";
        (*entity)["navigation_debug"] = {{"phase", phase},
                                         {"tick", 42},
                                         {"status", "stale navmesh"},
                                         {"sampled", sampled},
                                         {"has_path", sampled},
                                         {"remaining_distance", 1234.567},
                                         {"velocity", Double3{1, 2, 3}}};
        check(ComponentInspector::has_runtime_inspection(effective, schema, actor),
              "Editor visibility gate hid navigation-only progress");
        frame();
        frame();
        check(test::ui_targets.contains("navigation_agent:phase") &&
                  test::ui_targets.contains("navigation_agent:tick"),
              "Navigation-only entity did not expose readonly progress");
        check(test::ui_targets.contains("navigation_agent:remaining") == sampled &&
                  test::ui_targets.contains("navigation_agent:speed") == sampled,
              "Unprocessed/failed agent exposed invented metrics");
        check(test::ui_targets.contains("navigation_agent:failure") ==
                  (std::string_view(phase) == "Failed"),
              "Failure reason visibility wrong");
        for (const auto& [name, box] : test::ui_targets)
            if (name.starts_with("navigation_agent:"))
                check(box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x &&
                          box.minimum.y >= box.clip_minimum.y &&
                          box.maximum.y <= box.clip_maximum.y,
                      "Navigation state clipped at supported Inspector size/scale");
    }
    check(submissions == 0 && scene.document() == saved,
          "Readonly progress submitted/mutated authored values");
    ImGui::DestroyContext();
}
template <class Frame>
void edit_live_number(Frame&& frame, const std::string& key, const char* text) {
    auto& io = ImGui::GetIO();
    ImGuiID field_id = 0;
    for (unsigned attempt = 0;; ++attempt) {
        frame();
        const auto box = test::ui_targets.at(key);
        if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
            check(box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x,
                  "Live agent field clips horizontally at a supported scale");
            io.AddMousePosEvent(box.minimum.x + 30, (box.minimum.y + box.maximum.y) * .5f);
            field_id = box.id;
            break;
        }
        check(attempt < 20, "Live agent field could not be reached by Inspector scrolling");
        io.AddMousePosEvent((box.clip_minimum.x + box.clip_maximum.x) * .5f,
                            (box.clip_minimum.y + box.clip_maximum.y) * .5f);
        io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 2 : -2);
    }
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    if (ImGui::GetCurrentContext()->ActiveId != field_id)
        throw std::runtime_error("Numeric field did not acquire keyboard focus: " + key);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    frame();
    io.AddInputCharactersUTF8(text);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
}
void exercise_agent_live(float scale, ImVec2 size) {
    EngineContext engine(WorldRole::Runtime, false, {navigation_module({})});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Live agent"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_agent"}});
    auto navigation = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    const auto reference = engine.world().reference(scene.entity(actor).id()).value();
    const auto schema = scene.schema();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    unsigned submissions = 0;
    const auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Live navigation Inspector");
        inspector.draw_runtime(
            scene.effective_document(), schema, {}, actor,
            [&](const auto& type, const auto& property, const auto& value) {
                check(type == "forge.navigation_agent", "Unexpected live agent type");
                navigation->tune(reference, type, property, value);
                ++submissions;
                return true;
            },
            false);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    check(!test::ui_targets.contains("runtime-field:forge.navigation_agent:navmesh"),
          "Live agent Inspector permits changing its NavMesh asset");
    for (const auto* name : {"enabled", "has_destination", "speed", "stopping_distance",
                             "destination_x", "destination_y", "destination_z"})
        check(
            test::ui_targets.contains(std::string("runtime-field:forge.navigation_agent:") + name),
            "A supported live Navigation Agent field is missing");
    edit_live_number(frame, "runtime-field:forge.navigation_agent:speed", "3.25");
    edit_live_number(frame, "runtime-field:forge.navigation_agent:destination_x", "-4.5");
    check(submissions == 2 && scene.entity(actor).get<NavigationAgent>().speed == 3.25f &&
              scene.entity(actor).get<NavigationAgent>().destination_x == -4.5 &&
              navigation->agent_state(reference)->tick == 0,
          "Actual live agent field did not tune without advancing simulation");
    check(scene.undo() && !scene.entity(actor).has<NavigationAgent>(),
          "Live agent tuning created an authored history entry");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
void exercise_agent_keep_values() {
    const auto root =
        std::filesystem::current_path() / ("navigation-review-" + AssetId::generate().str());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{root};
    SceneDocument::create_project(root, "Navigation values");
    EngineContext author;
    Scene scene(author.world());
    SceneDocument document(scene);
    document.open_project(root);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Kept agent"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_agent"}});
    document.save();
    const auto before = scene.document();
    EngineContext runtime(WorldRole::Runtime, false, {navigation_module(root)});
    Scene live(runtime.world());
    RuntimeSimulation simulation(runtime.world(), live);
    live.restore_snapshot(scene.snapshot());
    auto navigation = std::static_pointer_cast<NavigationRuntime>(runtime.services().navigation());
    const auto reference = runtime.world().reference(live.entity(actor).id()).value();
    navigation->tune(reference, "forge.navigation_agent", "speed", 5);
    navigation->tune(reference, "forge.navigation_agent", "destination_x", 8);
    auto snapshot = live.effective_document();
    // A detached hostile/unsupported asset and moved pose must never enter the field review.
    for (auto& row : snapshot["entities"])
        if (row["id"] == actor) {
            row["components"]["forge.navigation_agent"]["navmesh"] = AssetId::generate();
            row["components"]["forge.local_translation"]["x"] = 999;
        }
    RuntimeTuningReview review;
    check(review.capture(scene, document, snapshot, live.schema(), actor, 0) &&
              review.fields().size() == 2 && scene.document() == before,
          "Navigation keep-values review lost fields or copied an asset/pose");
    for (std::size_t i = 0; i < review.fields().size(); ++i)
        if (review.fields()[i].property == "destination_x")
            review.select(i, false);
    navigation->tune(reference, "forge.navigation_agent", "speed", 9);
    check(!review.apply(scene, document, true) && scene.document() == before,
          "Navigation review applied while Play was locked");
    check(review.apply(scene, document, false) &&
              scene.entity(actor).get<NavigationAgent>().speed == 5 &&
              scene.entity(actor).get<NavigationAgent>().destination_x == 0 &&
              !scene.entity(actor).get<NavigationAgent>().navmesh.id,
          "Navigation review did not apply only the selected frozen scalar value");
    const auto after = scene.document();
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == after,
          "Kept navigation values lost ordinary Undo/Redo");
    document.save();
    document.open_scene(document.path());
    check(scene.document() == after, "Kept navigation values did not survive Save/reopen");
}
void exercise_avoidance(float scale, ImVec2 size) {
    EngineContext engine(WorldRole::Runtime, false, {navigation_module({})});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Live avoidance"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_agent"}});
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_avoidance"}});
    const auto schema = scene.schema();
    auto navigation = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    const auto reference = engine.world().reference(scene.entity(actor).id()).value();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    unsigned submissions = 0;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Avoidance Inspector");
        inspector.draw_runtime(
            scene.effective_document(), schema, {}, actor,
            [&](const auto& type, const auto& property, const auto& value) {
                check(type == "forge.navigation_avoidance", "Unexpected live type submitted");
                navigation->tune_avoidance(reference, property, value);
                ++submissions;
                return true;
            },
            false);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    check(test::ui_targets.contains("runtime-field:forge.navigation_agent:speed"),
          "Navigation Agent live Speed control is missing from the actual Inspector");
    check(!test::ui_targets.contains("runtime-field:forge.navigation_agent:navmesh"),
          "Live Navigation Agent exposed an asset replacement control");
    // Collapse the independent Agent section, as a user would before tuning avoidance.
    const auto header = test::ui_targets.at("runtime-component:forge.navigation_agent");
    io.AddMousePosEvent(header.minimum.x + 30, (header.minimum.y + header.maximum.y) * .5f);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    for (const auto& name : {"enabled", "horizon"}) {
        const auto box =
            test::ui_targets.at("runtime-field:forge.navigation_avoidance:" + std::string(name));
        check(box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x &&
                  box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y,
              "Live avoidance control clipped at supported scale/size");
    }
    edit_live_number(frame, "runtime-field:forge.navigation_avoidance:horizon", "1.25");
    if (submissions != 1 || scene.entity(actor).get<NavigationAvoidance>().horizon != 1.25f ||
        navigation->agent_state(reference)->phase != NavigationAgentPhase::Idle)
        throw std::runtime_error(
            "Actual avoidance input failed at scale " + std::to_string(scale) + " width " +
            std::to_string(size.x) + ": submissions=" + std::to_string(submissions) +
            " horizon=" + std::to_string(scene.entity(actor).get<NavigationAvoidance>().horizon));
    check(scene.undo() && !scene.entity(actor).has<NavigationAvoidance>(),
          "Live avoidance edit created an authored history entry");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        for (float scale : {1.f, 1.5f, 2.f})
            for (ImVec2 size : {ImVec2{420, 640}, ImVec2{1200, 900}}) {
                exercise(scale, size);
                exercise_avoidance(scale, size);
                exercise_agent_live(scale, size);
            }
        exercise_agent_keep_values();
        std::cout << "Navigation Live progress, failure/metric gating, scales and readonly "
                     "isolation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
