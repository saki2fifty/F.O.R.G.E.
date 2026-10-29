#include "editor/scene_lighting.hpp"
#include <backends/imgui_impl_sdlrenderer3.h>
#include <filesystem>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void interactions(float scale, ImVec2 display) {
    EngineContext engine;
    Scene scene(engine.world());
    auto extended = scene.document();
    extended["rendering"] = {{"version", 1u}, {"bloom", {{"extension", "retain"}}}};
    extended["rendering"]["auto_exposure"] = {{"extension", "retain"}};
    extended["rendering"]["temporal_anti_aliasing"] = {{"extension", "retain"}};
    extended["rendering"]["depth_of_field"] = {{"extension", "retain"}};
    scene.reset(extended);
    const auto original = scene.document();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    bool locked = false;
    bool color_controls = false;
    bool automatic_controls = false;
    bool fog_controls = false;
    bool temporal_controls = false;
    bool focus_controls = false;
    bool motion_controls = false;
    bool full_owner = false;
    std::string error;
    unsigned commits = 0;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({std::min(800 * scale, display.x - 20), display.y - 20},
                                 ImGuiCond_Always);
        if (full_owner) {
            bool open = true;
            ui::scene_lighting(open, scene, nullptr, locked);
        } else {
            ImGui::Begin("Scene lighting");
            try {
                const auto settings = scene_render_settings(scene.document());
                const auto commit = [&](const Json& patch) {
                    authoring_command(scene, "scene.rendering.set", patch);
                    ++commits;
                    error.clear();
                };
                if (motion_controls)
                    ui::scene_motion_controls(settings.motion_blur, locked, commit);
                else if (focus_controls)
                    ui::scene_focus_controls(settings.depth_of_field, locked, commit);
                else if (temporal_controls)
                    ui::scene_temporal_controls(settings.temporal_anti_aliasing, locked, commit);
                else if (fog_controls)
                    ui::scene_distance_fog_controls(settings.distance_fog, locked, commit);
                else if (automatic_controls)
                    ui::scene_auto_exposure_controls(settings.auto_exposure, locked, commit);
                else if (color_controls)
                    ui::scene_color_controls(settings.color_adjustments, locked, commit);
                else
                    ui::scene_bloom_controls(settings.bloom, locked, commit);
            } catch (const std::exception& exception) {
                error = exception.what();
                ui::field_error(error);
            }
            ImGui::End();
        }
        ImGui::Render();
    };
    auto click = [&](const char* key) {
        test::UiTarget box;
        bool visible = false;
        for (unsigned attempt = 0; attempt < 20; ++attempt) {
            frame();
            box = test::ui_targets.at(key);
            if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
                visible = true;
                break;
            }
            io.AddMousePosEvent(box.clip_minimum.x + 8, box.clip_maximum.y - 8);
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? .5f : -.5f);
            frame();
        }
        if (!visible)
            throw std::runtime_error(std::string("Display control could not be reached: ") + key +
                                     " at scale " + std::to_string(scale));
        if (!box.enabled || box.minimum.x < box.clip_minimum.x ||
            box.maximum.x > box.clip_maximum.x || box.minimum.y < box.clip_minimum.y ||
            box.maximum.y > box.clip_maximum.y)
            throw std::runtime_error(
                std::string(key) + " clipped/disabled at scale " + std::to_string(scale) + ": " +
                Json{{"box", {box.minimum.x, box.minimum.y, box.maximum.x, box.maximum.y}},
                     {"clip",
                      {box.clip_minimum.x, box.clip_minimum.y, box.clip_maximum.x,
                       box.clip_maximum.y}},
                     {"enabled", box.enabled}}
                    .dump());
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                            (box.minimum.y + box.maximum.y) / 2);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto type = [&](const char* key, const char* value) {
        click(key);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    for (unsigned i = 0; i < 4; ++i)
        frame();
    click("bloom:enabled");
    check(commits == 1 && scene_render_settings(scene.document()).bloom.enabled,
          "Bloom checkbox did not commit");
    check(scene.undo() && scene.document() == original && scene.redo(),
          "Bloom checkbox Undo failed");
    type("bloom:threshold", "2.5");
    type("bloom:intensity", "1.5");
    type("bloom:radius", "3.25");
    check(commits == 4 &&
              scene_render_settings(scene.document()).bloom ==
                  SceneBloom{true, 2.5f, 1.5f, 3.25f} &&
              scene.document()["rendering"]["bloom"]["extension"] == "retain",
          "Typed bloom fields did not preserve intent/extensions");
    const auto good = scene.document();
    type("bloom:radius", "100");
    check(!error.empty() && commits == 4 && scene.document() == good,
          "Invalid typed bloom settings changed Scene");
    // Rejection unwinds nested disabled/ID stacks; subsequent valid interaction works.
    type("bloom:radius", "4");
    check(error.empty() && commits == 5 &&
              scene_render_settings(scene.document()).bloom.radius == 4,
          "Bloom UI did not recover after rejected input");
    check(scene.undo() && scene.document() == good && scene.redo(), "Bloom field Undo failed");
    locked = true;
    frame();
    for (const char* key : {"bloom:enabled", "bloom:threshold", "bloom:intensity", "bloom:radius"})
        check(!test::ui_targets.at(key).enabled, "Bloom authored control enabled during Play");
    locked = false;
    color_controls = true;
    const auto before_color = scene.document();
    click("color:enabled");
    check(scene_render_settings(scene.document()).color_adjustments.enabled,
          "Color checkbox did not enable authored grading");
    type("color:saturation", "0");
    type("color:contrast", "1.5");
    type("color:filter", "0.5"); // Center lane of the real RGB field is green.
    const auto adjusted = scene.document();
    check(scene_render_settings(adjusted).color_adjustments ==
                  SceneColorAdjustments{true, {1, .5f, 1}, 0, 1.5f} &&
              adjusted["rendering"]["bloom"] == before_color["rendering"]["bloom"],
          "Typed color controls lost values or changed bloom");
    type("color:saturation", "3");
    check(!error.empty() && scene.document() == adjusted,
          "Rejected color input changed authored values");
    type("color:saturation", "0.5");
    check(error.empty() &&
              scene_render_settings(scene.document()).color_adjustments.saturation == .5f,
          "Color UI did not recover after rejected input");
    const auto before_reset = scene.document();
    click("color:reset");
    check(scene_render_settings(scene.document()).color_adjustments == SceneColorAdjustments{} &&
              scene.undo() && scene.document() == before_reset && scene.redo(),
          "Reset color adjustments did not use one reversible Scene operation");
    locked = true;
    frame();
    for (const char* key :
         {"color:enabled", "color:filter", "color:saturation", "color:contrast", "color:reset"})
        check(!test::ui_targets.at(key).enabled, "Authored color control enabled during Play");
    locked = false;
    automatic_controls = true;
    frame();
    click("exposure:section");
    const auto before_auto = scene.document();
    click("exposure:enabled");
    const auto enabled_auto = scene.document();
    check(scene_render_settings(enabled_auto).auto_exposure.enabled && scene.undo() &&
              scene.document() == before_auto && scene.redo(),
          "Automatic exposure checkbox bypassed Scene history");
    for (const auto& [key, text] :
         std::vector<std::pair<const char*, const char*>>{{"exposure:min_ev", "-4"},
                                                          {"exposure:max_ev", "4"},
                                                          {"exposure:low_percentile", "10"},
                                                          {"exposure:high_percentile", "90"},
                                                          {"exposure:target_gray", "0.2"},
                                                          {"exposure:speed_up", "2"},
                                                          {"exposure:speed_down", "5"}})
        type(key, text);
    check(error.empty() &&
              scene_render_settings(scene.document()).auto_exposure ==
                  SceneAutoExposure{true, -4, 4, .1f, .9f, .2f, 2, 5} &&
              scene.document()["rendering"]["auto_exposure"]["extension"] == "retain",
          "Automatic controls lost percent conversion, known fields or opaque data");
    const auto before_bad_auto = scene.document();
    type("exposure:low_percentile", "95");
    check(!error.empty() && scene.document() == before_bad_auto,
          "An inverted percentile edit mutated the Scene");
    type("exposure:low_percentile", "5");
    check(error.empty() &&
              scene_render_settings(scene.document()).auto_exposure.low_percentile == .05f,
          "Automatic input did not recover after rejected percentiles");
    const auto before_auto_reset = scene.document();
    click("exposure:reset");
    check(scene_render_settings(scene.document()).auto_exposure == SceneAutoExposure{} &&
              scene.undo() && scene.document() == before_auto_reset && scene.redo(),
          "Reset automatic exposure was not one reversible operation");
    locked = true;
    frame();
    for (const auto* key :
         {"exposure:enabled", "exposure:min_ev", "exposure:max_ev", "exposure:low_percentile",
          "exposure:high_percentile", "exposure:target_gray", "exposure:speed_up",
          "exposure:speed_down", "exposure:reset"})
        check(!test::ui_targets.at(key).enabled, "Automatic authoring control enabled during Play");
    locked = false;
    click("exposure:section");
    fog_controls = true;
    frame();
    click("fog:section");
    const auto before_fog = scene.document();
    click("fog:enabled");
    check(scene_render_settings(scene.document()).distance_fog.enabled && scene.undo() &&
              scene.document() == before_fog && scene.redo(),
          "Fog checkbox bypassed Scene history");
    type("fog:start", "5");
    type("fog:end", "30");
    type("fog:color", "0.25"); // Center lane of the real RGB field is green.
    const auto fogged = scene.document();
    check(error.empty() && scene_render_settings(fogged).distance_fog ==
                               SceneDistanceFog{true, {.5f, .25f, .5f}, 5, 30},
          "Typed fog controls lost distances or color");
    type("fog:end", "4");
    check(!error.empty() && scene.document() == fogged,
          "Rejected fog distance changed authored settings");
    type("fog:end", "40");
    check(error.empty() && scene_render_settings(scene.document()).distance_fog.end == 40,
          "Fog UI failed to recover after a rejected range");
    const auto before_fog_reset = scene.document();
    click("fog:reset");
    check(scene_render_settings(scene.document()).distance_fog == SceneDistanceFog{} &&
              scene.undo() && scene.document() == before_fog_reset && scene.redo(),
          "Reset fog was not one reversible operation");
    locked = true;
    frame();
    for (const auto* key : {"fog:enabled", "fog:color", "fog:start", "fog:end", "fog:reset"})
        check(!test::ui_targets.at(key).enabled, "Fog authored control enabled during Play");
    locked = false;
    click("fog:section");
    fog_controls = false;
    temporal_controls = true;
    frame();
    click("temporal:section");
    const auto before_temporal = scene.document();
    click("temporal:enabled");
    check(scene_render_settings(scene.document()).temporal_anti_aliasing.enabled && scene.undo() &&
              scene.document() == before_temporal && scene.redo(),
          "Temporal checkbox bypassed Scene history");
    type("temporal:stability", "0.9");
    const auto stable = scene.document();
    check(error.empty() &&
              scene_render_settings(stable).temporal_anti_aliasing ==
                  SceneTemporalAntiAliasing{true, .9f} &&
              stable["rendering"]["temporal_anti_aliasing"]["extension"] == "retain",
          "Temporal controls lost stability or opaque extensions");
    type("temporal:stability", "1");
    check(!error.empty() && scene.document() == stable, "Invalid temporal input changed Scene");
    type("temporal:stability", "0.7");
    check(error.empty() &&
              scene_render_settings(scene.document()).temporal_anti_aliasing.stability == .7f,
          "Temporal control failed to recover after rejected input");
    const auto before_temporal_reset = scene.document();
    click("temporal:reset");
    check(scene_render_settings(scene.document()).temporal_anti_aliasing ==
                  SceneTemporalAntiAliasing{} &&
              scene.undo() && scene.document() == before_temporal_reset && scene.redo(),
          "Temporal reset was not one reversible operation");
    locked = true;
    frame();
    for (const auto* key : {"temporal:enabled", "temporal:stability", "temporal:reset"})
        check(!test::ui_targets.at(key).enabled, "Temporal authored control enabled during Play");
    locked = false;
    click("temporal:section");
    temporal_controls = false;
    focus_controls = true;
    frame();
    click("focus:section");
    const auto before_focus = scene.document();
    click("focus:enabled");
    check(scene_render_settings(scene.document()).depth_of_field.enabled && scene.undo() &&
              scene.document() == before_focus && scene.redo(),
          "Focus checkbox bypassed Scene history");
    type("focus:focus_distance", "14");
    type("focus:focus_range", "3");
    type("focus:blur_radius", "6");
    const auto focused = scene.document();
    check(error.empty() &&
              scene_render_settings(focused).depth_of_field == SceneDepthOfField{true, 14, 3, 6} &&
              focused["rendering"]["depth_of_field"]["extension"] == "retain",
          "Typed focus controls lost values or extensions");
    type("focus:focus_range", "0");
    check(!error.empty() && scene.document() == focused, "Invalid focus range changed Scene");
    type("focus:focus_range", "4");
    check(error.empty() && scene_render_settings(scene.document()).depth_of_field.focus_range == 4,
          "Focus input did not recover after rejection");
    const auto before_focus_reset = scene.document();
    click("focus:reset");
    check(scene_render_settings(scene.document()).depth_of_field == SceneDepthOfField{} &&
              scene.undo() && scene.document() == before_focus_reset && scene.redo(),
          "Focus reset did not use atomic Scene history");
    locked = true;
    frame();
    for (const auto* key : {"focus:enabled", "focus:focus_distance", "focus:focus_range",
                            "focus:blur_radius", "focus:reset"})
        check(!test::ui_targets.at(key).enabled, "Focus authored controls enabled during Play");
    locked = false;
    click("focus:section");
    focus_controls = false;
    motion_controls = true;
    frame();
    click("motion:section");
    const auto before_motion = scene.document();
    click("motion:enabled");
    check(scene_render_settings(scene.document()).motion_blur.enabled && scene.undo() &&
              scene.document() == before_motion && scene.redo(),
          "Motion checkbox bypassed Scene history");
    type("motion:shutter", "0.75");
    type("motion:max_pixels", "24");
    const auto moving = scene.document();
    check(error.empty() &&
              scene_render_settings(moving).motion_blur == SceneMotionBlur{true, .75f, 24},
          "Typed motion controls lost values");
    type("motion:shutter", "2");
    check(!error.empty() && scene.document() == moving, "Invalid shutter changed Scene");
    type("motion:shutter", "0.5");
    check(error.empty() && scene_render_settings(scene.document()).motion_blur.shutter == .5f,
          "Motion input did not recover after rejection");
    const auto before_motion_reset = scene.document();
    click("motion:reset");
    check(scene_render_settings(scene.document()).motion_blur == SceneMotionBlur{} &&
              scene.undo() && scene.document() == before_motion_reset && scene.redo(),
          "Motion reset was not one reversible operation");
    locked = true;
    frame();
    for (const auto* key :
         {"motion:enabled", "motion:shutter", "motion:max_pixels", "motion:reset"})
        check(!test::ui_targets.at(key).enabled, "Motion authored control enabled during Play");
    locked = false;
    click("motion:section");
    motion_controls = false;
    // Exercise the real Scene lighting owner, including its Problems integration,
    // rather than inferring recovery from the callback used above.
    locked = false;
    full_owner = true;
    context.task.focus_document("material", "Material");
    type("color:contrast", "3");
    check(context.problems.size() == 1 &&
              context.problems.items().front().key == "scene.lighting" &&
              !context.problems.items().front().occurred_at.empty(),
          "Actual Scene lighting rejection did not create a timestamped Problem");
    type("color:contrast", "1.25");
    check(context.problems.size() == 0 &&
              scene_render_settings(scene.document()).color_adjustments.contrast == 1.25f &&
              context.task.owner == ui::DocumentTask::Scene,
          "Corrected Scene lighting left a stale Problem or routed Save/history to an asset");
    click("exposure:section");
    context.task.focus_document("material", "Material");
    type("exposure:max_ev", "21");
    check(context.problems.size() == 1 && !context.problems.items().front().occurred_at.empty(),
          "Actual automatic exposure rejection did not create a timestamped Problem");
    type("exposure:max_ev", "6");
    check(context.problems.size() == 0 &&
              scene_render_settings(scene.document()).auto_exposure.max_ev == 6 &&
              context.task.owner == ui::DocumentTask::Scene,
          "Corrected automatic exposure retained its Problem or changed asset-document ownership");
    click("exposure:section");
    click("fog:section");
    context.task.focus_document("material", "Material");
    type("fog:end", "0");
    check(context.problems.size() == 1 && !context.problems.items().front().occurred_at.empty(),
          "Actual fog rejection lacked a timestamped Problem");
    type("fog:end", "80");
    check(context.problems.size() == 0 &&
              scene_render_settings(scene.document()).distance_fog.end == 80 &&
              context.task.owner == ui::DocumentTask::Scene,
          "Corrected fog retained a Problem or changed document ownership");
    click("fog:section");
    click("temporal:section");
    context.task.focus_document("material", "Material");
    type("temporal:stability", "1");
    check(context.problems.size() == 1 && !context.problems.items().front().occurred_at.empty(),
          "Actual temporal rejection lacked a timestamped Problem");
    type("temporal:stability", "0.8");
    check(context.problems.size() == 0 &&
              scene_render_settings(scene.document()).temporal_anti_aliasing.stability == .8f &&
              context.task.owner == ui::DocumentTask::Scene,
          "Corrected temporal input retained its Problem or changed document ownership");
    click("temporal:section");
    click("focus:section");
    context.task.focus_document("material", "Material");
    type("focus:blur_radius", "13");
    check(context.problems.size() == 1 && !context.problems.items().front().occurred_at.empty(),
          "Actual focus rejection lacked a timestamped Problem");
    type("focus:blur_radius", "8");
    check(context.problems.size() == 0 &&
              scene_render_settings(scene.document()).depth_of_field.blur_radius == 8 &&
              context.task.owner == ui::DocumentTask::Scene,
          "Corrected focus input retained its Problem or document ownership");
    click("focus:section");
    click("motion:section");
    context.task.focus_document("material", "Material");
    type("motion:max_pixels", "33");
    check(context.problems.size() == 1 && !context.problems.items().front().occurred_at.empty(),
          "Actual motion rejection lacked a timestamped Problem");
    type("motion:max_pixels", "16");
    check(context.problems.size() == 0 &&
              scene_render_settings(scene.document()).motion_blur.max_pixels == 16 &&
              context.task.owner == ui::DocumentTask::Scene,
          "Corrected motion retained its Problem or document ownership");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
void capture(const std::filesystem::path& root, std::string_view kind = {}) {
    const bool fog = kind == "--fog", temporal = kind == "--temporal", focus = kind == "--focus",
               motion = kind == "--motion";
    std::filesystem::create_directories(root);
    const int capture_height = fog ? 360 : temporal ? 420 : focus ? 440 : motion ? 400 : 900;
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(900, capture_height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    check(bool(surface), SDL_GetError());
    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    check(bool(renderer), SDL_GetError());
    const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), ImGui::DestroyContext);
    check(ImGui_ImplSDLRenderer3_Init(renderer.get()),
          "Native lighting capture backend unavailable");
    struct Backend {
        ~Backend() { ImGui_ImplSDLRenderer3_Shutdown(); }
    } backend;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {900, float(capture_height)};
    io.DeltaTime = 1.f / 60;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16) != nullptr,
          "Actual lighting font missing");
    ui::style(1);
    ui::EditorUiContext ui_context;
    ui::ContextScope scope(ui_context);
    EngineContext engine;
    Scene scene(engine.world());
    authoring_command(
        scene, "scene.rendering.set",
        motion     ? Json{{"motion_blur", {{"enabled", true}, {"shutter", .5}, {"max_pixels", 32}}}}
        : focus    ? Json{{"depth_of_field",
                           {{"enabled", true},
                            {"focus_distance", 10},
                            {"focus_range", 5},
                            {"blur_radius", 8}}}}
        : temporal ? Json{{"temporal_anti_aliasing", {{"enabled", true}, {"stability", .8}}}}
        : fog ? Json{{"distance_fog",
                      {{"enabled", true}, {"color", {.25, .35, .5}}, {"start", 12}, {"end", 34}}}}
              : Json{{"auto_exposure", {{"enabled", true}}}});
    for (unsigned frame = 0; frame < 3; ++frame) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({880, float(capture_height - 20)});
        ImGui::Begin("Scene lighting", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const auto commit = [&](const Json& patch) {
            authoring_command(scene, "scene.rendering.set", patch);
        };
        if (motion)
            ui::scene_motion_controls(scene_render_settings(scene.document()).motion_blur, false,
                                      commit);
        else if (focus)
            ui::scene_focus_controls(scene_render_settings(scene.document()).depth_of_field, false,
                                     commit);
        else if (temporal)
            ui::scene_temporal_controls(
                scene_render_settings(scene.document()).temporal_anti_aliasing, false, commit);
        else if (fog)
            ui::scene_distance_fog_controls(scene_render_settings(scene.document()).distance_fog,
                                            false, commit);
        else
            ui::scene_auto_exposure_controls(scene_render_settings(scene.document()).auto_exposure,
                                             false, commit);
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer.get(), 9, 15, 18, 255);
        SDL_RenderClear(renderer.get());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        SDL_RenderPresent(renderer.get());
    }
    check(SDL_SavePNG(surface.get(), (root / (motion     ? "motion-blur-controls.png"
                                              : focus    ? "depth-of-field-controls.png"
                                              : temporal ? "temporal-antialiasing-controls.png"
                                              : fog      ? "distance-fog-controls.png"
                                                         : "automatic-exposure.png"))
                                         .string()
                                         .c_str()),
          SDL_GetError());
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 ||
            (argc == 3 &&
             (std::string_view(argv[2]) == "--fog" || std::string_view(argv[2]) == "--temporal" ||
              std::string_view(argv[2]) == "--focus" || std::string_view(argv[2]) == "--motion"))) {
            capture(std::filesystem::absolute(argv[1]),
                    argc == 3 ? std::string_view(argv[2]) : std::string_view{});
            return 0;
        }
        check(argc == 1, "Expected optional native lighting capture directory");
        for (float scale : {1.f, 1.5f, 2.f})
            for (const auto display : {ImVec2{2400, 1700}, ImVec2{960, 640}})
                interactions(scale, display);
        std::cout << "Bloom/color/automatic exposure actual input, rejection/recovery, read-only "
                     "and Scene Undo PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
