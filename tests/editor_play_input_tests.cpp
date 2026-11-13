#include "editor/actions.hpp"
#include "editor_input_workflow.hpp"
#include <iostream>
#include <stdexcept>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(bool reject_start, bool reject_every_click) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {400, 300};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto workflow = test::EditorInputWorkflow::play_click_probe();
    test::play_action_invocations = 0;
    Json state{{"playing", false},
               {"control_ready", false},
               {"scene", {{"entities", Json::array()}}},
               {"play_status", "Stopped. Authored scene preserved."},
               {"play_log", "Log retained from an earlier successful Play"},
               {"problems", Json::array({{{"text", "Earlier unrelated graph error"}}})}};
    unsigned calls = 0, retries = 0;
    bool exhausted = false;
    ui::EditorActions actions;
    ui::EditorAction play;
    play.id = "play";
    play.label = "Play";
    play.execute = [&] {
        ++calls;
        if (reject_start)
            throw std::runtime_error("Native startup rejected");
        state["playing"] = true;
        state["control_ready"] = true;
    };
    actions.entries.push_back(std::move(play));
    auto draw = [&](unsigned frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({400, 300});
        ImGui::Begin("Toolbar");
        // The target exists/enables on observation, then becomes disabled across
        // the first press/release. This reproduces an asynchronous action gate.
        actions.entries.front().available =
            reject_every_click ? !io.MouseDown[0] : (frame == 0 || frame > 10);
        try {
            actions.icon("play", ui::Icon::Play);
        } catch (const std::exception& e) {
            state["play_log"] = e.what();
        }
        ImGui::End();
        ImGui::Render();
    };
    draw(0);
    for (unsigned frame = 1; frame <= 120 && !workflow.done(); ++frame) {
        workflow.input();
        draw(frame);
        try {
            workflow.finish(
                state, [](const auto&) {},
                [&](const Json& report) {
                    for (const auto& entry : report.at("trace"))
                        if (entry.value("operation", "") == "retry unaccepted toolbar click")
                            retries = std::max(retries, entry.at("attempt").get<unsigned>() - 1);
                });
        } catch (const std::exception& e) {
            require(reject_every_click &&
                        std::string(e.what()).find("after three clicks") != std::string::npos,
                    "Unexpected Play input workflow failure");
            exhausted = true;
            break;
        }
        SDL_Delay(15);
    }
    require(calls == test::play_action_invocations, "Real action receipt lost a callback");
    if (reject_every_click)
        require(exhausted && calls == 0 && retries == 2, "Unaccepted clicks did not stop at three");
    else if (reject_start)
        require(!workflow.done() && calls == 1 && state.at("play_log") == "Native startup rejected",
                "A real startup failure was retried or reported as ready");
    else
        require(workflow.done() && calls == 1 && retries > 0,
                "Unaccepted Play click did not recover through the real toolbar");
    ImGui::DestroyContext();
}
void action_receipt(bool reject_click) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {400, 300};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    const std::string id = "Create / Cube";
    test::action_invocations.clear();
    auto workflow = test::EditorInputWorkflow::action_click_probe(id);
    unsigned calls = 0, frames = 0, draws = 0;
    ui::EditorActions actions;
    ui::EditorAction action;
    action.id = id;
    action.label = "Cube";
    action.execute = [&] {
        ++calls;
        SDL_Delay(110); // Downstream realization outlasts the minimum input dwell.
    };
    actions.entries.push_back(std::move(action));
    const auto draw = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({400, 300});
        ImGui::Begin("Actions");
        actions.entries.front().available = !reject_click || draws == 0;
        ++draws;
        actions.item(id);
        ImGui::End();
        ImGui::Render();
    };
    draw();
    for (; frames < 12 && !workflow.done(); ++frames) {
        workflow.input();
        draw();
        workflow.finish(Json::object(), [](const auto&) {}, [](const Json&) {});
        SDL_Delay(15);
    }
    if (reject_click)
        require(!workflow.done() && calls == 0,
                "Action input completed without observing its real callback");
    else
        require(workflow.done() && calls == 1 && frames < 8,
                "Completed action waited for extra frames instead of its callback");
    ImGui::DestroyContext();
}
void menu_input(bool compact, bool entity = false, bool reject_first = false) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {400, 300};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto workflow = entity ? test::EditorInputWorkflow::create_menu_probe()
                           : test::EditorInputWorkflow::menu_click_probe();
    bool opened = false;
    unsigned compact_clicks = 0, draws = 0, retries = 0;
    const auto draw = [&] {
        test::ui_targets.clear();
        opened = false;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({400, 300});
        ImGui::Begin("Menu host", nullptr, ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar()) {
            const bool root = !compact || ImGui::BeginMenu("Menu");
            if (compact)
                FORGE_UI_PROBE("menu:overflow");
            if (root) {
                const bool file = ImGui::BeginMenu(entity ? "Entity" : "File",
                                                   !reject_first || draws == 0 || draws > 10);
                FORGE_UI_PROBE(entity ? "menu:Entity" : "menu:File");
                opened = file;
                if (file) {
                    if (entity) {
                        const bool create = ImGui::BeginMenu("Create");
                        FORGE_UI_PROBE("menu:Create");
                        if (create)
                            ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                if (compact)
                    ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        ImGui::End();
        ImGui::Render();
        ++draws;
    };
    draw();
    for (unsigned frame = 0; frame < 100 && !workflow.done(); ++frame) {
        workflow.input();
        draw();
        workflow.finish(
            Json::object(), [](const auto&) {},
            [&](const Json& report) {
                compact_clicks = 0;
                for (const auto& entry : report.at("trace")) {
                    compact_clicks += entry.value("operation", "") == "open compact menu";
                    retries += entry.value("operation", "") == "retry Entity menu click";
                }
            });
        SDL_Delay(15);
    }
    require(workflow.done() && opened && compact_clicks == unsigned(compact),
            "Native input driver did not open the requested responsive menu");
    if (reject_first)
        require(retries != 0, "Rejected Entity menu click did not exercise retry");
    ImGui::DestroyContext();
}
void readiness() {
    ImGui::CreateContext();
    auto workflow = test::EditorInputWorkflow::readiness_probe("cube");
    require(!workflow.waiting_for_graph_compilation(),
            "Ordinary authored readiness requested a shader wait");
    Json state{{"scene", {{"entities", Json::array()}}}, {"selected", ""}};
    workflow.input();
    workflow.finish(state, [](const auto&) {}, [](const Json&) {});
    require(!workflow.done(), "Readiness passed before the authored result existed");
    state["scene"]["entities"].push_back({{"id", "cube"}});
    state["selected"] = "cube";
    workflow.input();
    workflow.finish(state, [](const auto&) {}, [](const Json&) {});
    require(workflow.done(), "Ready authored state waited for unrelated input frames");
    auto graph = test::EditorInputWorkflow::readiness_probe("graph-ready");
    state = {{"scene", {{"entities", Json::array()}}},
             {"graph_dirty", true},
             {"graph_preview_current", false},
             {"graph_gpu_ready", false},
             {"graph_error", ""}};
    graph.input();
    graph.finish(state, [](const auto&) {}, [](const Json&) {});
    require(graph.waiting_for_graph_compilation(), "Pending graph did not retain its compile wait");
    state["graph_dirty"] = false;
    state["graph_preview_current"] = state["graph_gpu_ready"] = true;
    graph.input();
    graph.finish(state, [](const auto&) {}, [](const Json&) {});
    require(graph.done() && !graph.waiting_for_graph_compilation(),
            "Accepted graph retained its compiler wait");
    ImGui::DestroyContext();
}
void capture_preparation() {
    ImGui::CreateContext();
    auto workflow = test::EditorInputWorkflow::capture_probe();
    const Json state{{"ui_scale", 1.0}};
    unsigned images = 0;
    bool recorded = false;
    workflow.input();
    // A completed native render frame can outlast the ordinary input deadline.
    SDL_Delay(12500);
    for (unsigned frame = 0; frame < 8 && !workflow.done(); ++frame) {
        if (frame)
            workflow.input();
        workflow.finish(
            state,
            [&](const auto& name) {
                if (name == "0-prepared-frame")
                    ++images;
            },
            [&](const Json& report) { recorded = report.at("ok").get<bool>(); });
    }
    require(workflow.done() && recorded && images == 1,
            "Completed capture was charged to the ordinary input deadline");
    ImGui::DestroyContext();
}
void recovery_choice(bool present) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto workflow = test::EditorInputWorkflow::recovery_probe();
    unsigned choices = 0, draws = 0;
    bool recorded_choice = false;
    const auto draw = [&] {
        ImGui::NewFrame();
        ImGui::Begin("Recovery host");
        if (present && draws == 0)
            ImGui::OpenPopup("Recovery available");
        if (ImGui::BeginPopupModal("Recovery available", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ui::button("Keep saved scene", "Keep the authored scene")) {
                ++choices;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::Render();
        ++draws;
    };
    draw();
    for (unsigned frame = 0; frame < 40 && !workflow.done(); ++frame) {
        workflow.input();
        draw();
        workflow.finish(
            Json::object(), [](const auto&) {},
            [&](const Json& report) {
                for (const auto& row : report.at("trace"))
                    recorded_choice |= row.value("operation", "") == "recovery choice clicked";
            });
        SDL_Delay(15);
    }
    require(workflow.done() && choices == unsigned(present) && recorded_choice == present,
            "Recovery handling skipped a visible choice or clicked without a prompt");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        require(SDL_Init(0), "SDL timer initialization failed");
        exercise(false, false);
        exercise(true, false);
        exercise(false, true);
        action_receipt(false);
        action_receipt(true);
        menu_input(false);
        menu_input(true);
        menu_input(false, true, true);
        menu_input(true, true);
        readiness();
        capture_preparation();
        recovery_choice(false);
        recovery_choice(true);
        SDL_Quit();
        std::cout << "Real toolbar receipt/retry bound/startup failure preservation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
