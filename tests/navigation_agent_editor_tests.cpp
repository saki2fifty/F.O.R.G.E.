#include "component_inspector.hpp"
#include "imgui_software_capture.hpp"
#include "runtime_tuning_review.hpp"
#include <forge/navigation.hpp>
#include <forge/runtime.hpp>
#include <imgui_internal.h>
#include <iostream>
#include <string_view>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class Frame> test::UiTarget visible_live_item(Frame&& frame, const std::string& key) {
    auto& io = ImGui::GetIO();
    for (unsigned attempt = 0;; ++attempt) {
        frame();
        const auto box = test::ui_targets.at(key);
        if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
            check(box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x,
                  "Live agent field clips horizontally at a supported scale");
            return box;
        }
        check(attempt < 20, "Live agent field could not be reached by Inspector scrolling");
        io.AddMousePosEvent((box.clip_minimum.x + box.clip_maximum.x) * .5f,
                            (box.clip_minimum.y + box.clip_maximum.y) * .5f);
        io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 2 : -2);
    }
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
        (*entity)["navigation_debug"] = {
            {"phase", phase},
            {"tick", 42},
            {"status", "stale navmesh"},
            {"sampled", sampled},
            {"has_path", sampled},
            {"remaining_distance", 1234.567},
            {"velocity", Double3{1, 2, 3}},
            {"position", Double3{1000, -2000, 37.125}},
            {"projected_destination", Double3{-8, .1, 37.125}},
            {"diagnostic", "NavigationAgent requires an entity without a physics body or enabled "
                           "character controller"}};
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
        check(test::ui_targets.contains("navigation_agent:coordinates") == sampled,
              "World coordinate disclosure does not follow valid sample/path state");
        check(test::ui_targets.contains("navigation_agent:failure") ==
                  (std::string_view(phase) == "Failed"),
              "Failure reason visibility wrong");
        check(test::ui_targets.contains("navigation_agent:diagnostic") ==
                  (std::string_view(phase) == "Failed"),
              "Concrete navigation reason leaked into another progress phase");
        if (sampled) {
            const auto click_coordinates = [&] {
                const auto box = visible_live_item(frame, "navigation_agent:coordinates");
                io.AddMousePosEvent(box.minimum.x + 10, (box.minimum.y + box.maximum.y) * .5f);
                frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                frame();
            };
            check(!test::ui_targets.contains("navigation_agent:position"),
                  "Coordinates crowded progress before expanding their disclosure");
            click_coordinates();
            check(test::ui_targets.contains("navigation_agent:position") &&
                      test::ui_targets.contains("navigation_agent:goal"),
                  "Actual coordinate disclosure omitted position/projected goal");
            (void)visible_live_item(frame, "navigation_agent:position");
            (void)visible_live_item(frame, "navigation_agent:goal");
            click_coordinates();
        }
        std::vector<std::string> progress;
        for (const auto& [name, box] : test::ui_targets)
            if (name.starts_with("navigation_agent:"))
                progress.push_back(name);
        for (const auto& name : progress)
            (void)visible_live_item(frame, name);
    }
    (*entity)["components"] = Json::object();
    entity->erase("navigation_debug");
    for (const char* phase : {"Pending", "Running", "Disabled", "Failed"}) {
        const bool failed = std::string_view(phase) == "Failed";
        (*entity)["navigation_behavior_debug"] = {
            {"phase", phase},
            {"state", 2},
            {"state_name", "Patrol"},
            {"elapsed", .25},
            {"transitions", 4},
            {"last_transition", 7},
            {"tick", 42},
            {"diagnostic", failed ? "Anchor is unavailable" : ""},
            {"retained_last_good", failed},
            {"resource_diagnostic", "Replacement rejected"}};
        check(ComponentInspector::has_runtime_inspection(effective, schema, actor),
              "Behavior-only progress was hidden");
        frame();
        frame();
        check(test::ui_targets.contains("navigation_behavior:phase") &&
                  test::ui_targets.contains("navigation_behavior:state") &&
                  test::ui_targets.contains("navigation_behavior:diagnostic") == failed &&
                  test::ui_targets.contains("navigation_behavior:last_good") == failed,
              "Behavior progress/diagnostic visibility is incorrect");
        (void)visible_live_item(frame, "navigation_behavior:state");
    }
    check(submissions == 0 && scene.document() == saved,
          "Readonly progress submitted/mutated authored values");
    ImGui::DestroyContext();
}
template <class Frame>
void edit_live_number(Frame&& frame, const std::string& key, const char* text) {
    auto& io = ImGui::GetIO();
    const auto box = visible_live_item(frame, key);
    const auto field_id = box.id;
    io.AddMousePosEvent(box.minimum.x + 30, (box.minimum.y + box.maximum.y) * .5f);
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
                check(type == "forge.navigation_agent" || type == "forge.navigation_behavior" ||
                          type == "forge.navigation_link_collision",
                      "Unexpected live navigation type");
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
    check(!test::ui_targets.contains("runtime-field:forge.navigation_agent:movement"),
          "Live agent Inspector permits changing its movement owner");
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
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_behavior"}});
    frame();
    frame();
    for (const char* field : {"graph", "target", "anchor"})
        check(!test::ui_targets.contains(std::string("runtime-field:forge.navigation_behavior:") +
                                         field),
              "Live behavior exposes reference/identity replacement");
    const auto enabled =
        visible_live_item(frame, "runtime-field:forge.navigation_behavior:enabled");
    io.AddMousePosEvent(enabled.minimum.x + 8, (enabled.minimum.y + enabled.maximum.y) * .5f);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    check(submissions == 3 && !scene.entity(actor).get<NavigationBehavior>().enabled,
          "Actual live behavior checkbox did not use the navigation owner");
    check(scene.undo() && !scene.entity(actor).has<NavigationBehavior>() &&
              scene.entity(actor).has<NavigationAgent>(),
          "Live behavior tuning created authored history");
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_link_collision"}});
    frame();
    frame();
    for (const auto* name :
         {"enabled", "radius", "straight_height", "clearance", "mask", "include_sensors"})
        check(test::ui_targets.contains(
                  std::string("runtime-field:forge.navigation_link_collision:") + name),
              "Live navigation collision field is unavailable");
    edit_live_number(frame, "runtime-field:forge.navigation_link_collision:radius", "0.45");
    edit_live_number(frame, "runtime-field:forge.navigation_link_collision:straight_height", "1.4");
    edit_live_number(frame, "runtime-field:forge.navigation_link_collision:clearance", "0.03");
    edit_live_number(frame, "runtime-field:forge.navigation_link_collision:mask", "0");
    const auto sensor =
        visible_live_item(frame, "runtime-field:forge.navigation_link_collision:include_sensors");
    io.AddMousePosEvent(sensor.minimum.x + 8, (sensor.minimum.y + sensor.maximum.y) * .5f);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    const auto collision = scene.entity(actor).get<NavigationLinkCollision>();
    check(submissions == 8 && collision.radius == .45f && collision.straight_height == 1.4f &&
              collision.clearance == .03f && collision.mask == 0 && collision.include_sensors &&
              navigation->agent_state(reference)->tick == 0,
          "Actual live collision controls did not use the existing navigation owner");
    check(scene.undo() && !scene.entity(actor).has<NavigationLinkCollision>() &&
              scene.entity(actor).has<NavigationAgent>(),
          "Live collision tuning created authored history");
    check(scene.undo() && !scene.entity(actor).has<NavigationAgent>(),
          "Live agent tuning created an authored history entry");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
void exercise_agent_keep_values(bool prefab) {
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
    auto actor = authoring_command(scene, "entity.create", {{"name", "Kept agent"}})
                     .at("selected")
                     .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_agent"}});
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_link_collision"}});
    if (prefab) {
        const auto definition = create_prefab_source(scene, actor);
        const auto asset =
            document.prefabs().create(scene, definition, "Assets/Traveler.prefab.json");
        actor = instantiate_prefab(scene, asset);
    }
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
    navigation->tune(reference, "forge.navigation_link_collision", "radius", .75);
    navigation->tune(reference, "forge.navigation_link_collision", "mask", 7u);
    navigation->tune(reference, "forge.navigation_link_collision", "include_sensors", true);
    const auto checkpoint = navigation->checkpoint();
    check(checkpoint.at("version").get<unsigned>() == 6u,
          "Live collision values did not select semantic recovery version 6");
    {
        EngineContext recovered(WorldRole::Runtime, false, {navigation_module(root)});
        Scene restored(recovered.world());
        restored.restore_snapshot(live.snapshot());
        RuntimeSimulation resumed(recovered.world(), restored);
        auto service =
            std::static_pointer_cast<NavigationRuntime>(recovered.services().navigation());
        service->restore(checkpoint);
        check(restored.entity(actor).get<NavigationLinkCollision>() ==
                  live.entity(actor).get<NavigationLinkCollision>(),
              "Recovery lost native collision settings behind Prefab intent");
        const auto before_rejection = restored.effective_document();
        for (const bool downgrade : {false, true}) {
            auto invalid = checkpoint;
            if (downgrade)
                invalid["version"] = 5;
            else
                for (auto& row : invalid["agents"])
                    if (!row.at("collision").is_null())
                        row["collision"]["config"]["mask"] = .5;
            bool refused = false;
            try {
                service->restore(invalid);
            } catch (const std::exception&) {
                refused = true;
            }
            check(refused && restored.effective_document() == before_rejection,
                  "Invalid/downgraded collision recovery changed native values");
        }
    }
    auto snapshot = live.effective_document();
    // A detached hostile/unsupported asset and moved pose must never enter the field review.
    for (auto& row : snapshot["entities"])
        if (row["id"] == actor) {
            row["components"]["forge.navigation_agent"]["navmesh"] = AssetId::generate();
            row["components"]["forge.local_translation"]["x"] = 999;
        }
    RuntimeTuningReview review;
    check(review.capture(scene, document, snapshot, live.schema(), actor, 0) &&
              review.fields().size() == 5 && scene.document() == before,
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
              !scene.entity(actor).get<NavigationAgent>().navmesh.id &&
              scene.entity(actor).get<NavigationLinkCollision>() ==
                  NavigationLinkCollision{true, .75f, 1.2f, .02f, 7, true},
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
void capture_collision(const std::filesystem::path& image, unsigned movement = 0) {
    const bool character = movement != 0;
    const auto root = image.parent_path() / ("project-" + AssetId::generate().str());
    SceneDocument::create_project(root, "Navigation collision controls");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Crossing agent"}})
                           .at("selected")
                           .get<std::string>();
    for (const auto* type :
         {"forge.navigation_agent", movement == 2 ? "forge.character_controller_2d"
                                    : character   ? "forge.character_controller"
                                                  : "forge.navigation_link_collision"})
        authoring_command(scene, "component.add", {{"entity", actor}, {"component", type}});
    if (movement == 2)
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.character_controller_2d"},
                           {"field", "mode"},
                           {"value", 1}});
    const int height = character ? 720 : 460;
    test::ImGuiSoftwareCapture capture(780, height);
    ui::style(1);
    ui::EditorUiContext context;
    context.scene = &scene;
    ui::ContextScope scope(context);
    ComponentInspector inspector;
    inspector.fixture_focus_component =
        character ? "forge.navigation_agent" : "forge.navigation_link_collision";
    auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({764, float(height - 16)}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw(scene, project, actor);
        ImGui::End();
        capture.render();
    };
    for (unsigned i = 0; i != 4; ++i)
        frame();
    if (character) {
        test::observe_ui = true;
        frame();
        const auto click = [&](const test::UiTarget& target) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                                (target.minimum.y + target.maximum.y) * .5f);
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        click(visible_live_item(frame, "component-field:forge.navigation_agent:movement"));
        frame();
        frame(); // Let the native popup settle before reading its item rectangle.
        const auto choice = "property:choice:movement:" + std::to_string(movement);
        check(test::ui_targets.contains(choice),
              "Character movement choice is absent from the native Inspector");
        click(test::ui_targets.at(choice));
        check(scene.entity(actor).get<NavigationAgent>().movement == movement,
              "Native movement choice did not use the authoring property owner");
        check(scene.undo() && scene.entity(actor).get<NavigationAgent>().movement == 0 &&
                  scene.redo() && scene.entity(actor).get<NavigationAgent>().movement == movement,
              "Character movement selection lost its single Undo/Redo operation");
        frame();
        test::observe_ui = false;
    }
    capture.save(image);
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && (std::string_view(argv[1]) == "--capture-collision" ||
                          std::string_view(argv[1]) == "--capture-character" ||
                          std::string_view(argv[1]) == "--capture-planar-character")) {
            capture_collision(std::filesystem::absolute(argv[2]),
                              std::string_view(argv[1]) == "--capture-planar-character" ? 2
                              : std::string_view(argv[1]) == "--capture-character"      ? 1
                                                                                        : 0);
            return 0;
        }
        check(argc == 1, "Unexpected navigation controls fixture arguments");
        for (float scale : {1.f, 1.5f, 2.f})
            for (ImVec2 size : {ImVec2{420, 640}, ImVec2{1200, 900}}) {
                exercise(scale, size);
                exercise_avoidance(scale, size);
                exercise_agent_live(scale, size);
            }
        exercise_agent_keep_values(false);
        exercise_agent_keep_values(true);
        std::cout << "Navigation Live progress, failure/metric gating, scales and readonly "
                     "isolation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
