// Editor SDK Play acceptance workflow.
//
// Drives the editor through the same standalone reference flow as
// tests/reference_game_workflow.hpp but on the live editor's separate-process
// PlaySession. Inputs are real SDL events; observations are real PlaySession
// snapshot fields plus the native Rml focus tree. No fabricated UI fields.
//
// Field contract (verified, do not invent alternatives):
//   play.ui_snapshot()            root keys: version/session/generation/
//                                 revision/documents/errors. Each document:
//                                 entity/asset/instance/visible/layer/model/
//                                 commands. The reference ui_id document
//                                 model carries page/interactions/message/
//                                 binding (samples/reference_game/gameplay.cpp67-76).
//   play.effective_snapshot()     root asset_id, entities[] with
//                                 components["forge.local_translation"]{x,y,z}.
//   play.status()                 std::string, NOT a JSON model with `message`.
//                                 Save confirmation lives on the UI document
//                                 model. Asset id at scene root, not "scene".
//   Rml focus                     Rml::GetContext(0)->GetFocusElement() with
//                                 IsVisible(true)/GetTagName()=="button"/
//                                 GetInnerRML()==label.
//
// Flow (mirrors reference_game_workflow.hpp182-342):
//   toolbarPlay (icon:play) -> main page -> New Game -> play +
//   level_scene + relative mouse capture -> E beacon (interactions==1)
//   -> W move z > 0.5 from rest -> record save_z -> Escape -> Options
//   -> Rebind Jump -> "Press a key" -> J -> "key.j" -> Apply ->
//   != "key.j" -> Back -> Resume -> J jump (y rises from baseline) ->
//   Escape -> Save Game -> model.message "Game saved" -> record
//   save_state -> Main Menu (menu asset_id, generation change) ->
//   Quit -> !play.active -> toolbarPlay -> main -> Continue ->
//   play + level_scene + restored interactions + non-default z +
//   J jump proves persisted binding -> Escape -> Quit ->
//   !play.active. Done only after that final Quit.

#pragma once
#include "../samples/reference_game/ids.hpp"
#include "play.hpp"
#include "ui_probe.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <forge/build.hpp>
#include <forge/scene.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace forge::test {

using Json = nlohmann::json;
using CaptureFn = std::function<void(std::string_view, const Json&)>;

// Drives the editor's SDK Play acceptance flow. Callers must:
//   * Construct once with the evidence directory.
//   * set_capture with a callback that performs a real GPU framebuffer
//     readback via fixture.capture(device, context, rtv, true, name) AND
//     writes the trace JSON. The callback must NOT fabricate metadata.
//   * set_failure_capture before frame().
//   * Call frame() once per render tick at the input/render boundary,
//     BEFORE SDL_PollEvent. pass the live play, window, the fixture
//     has_staged() flag, and any pending native capture sink.
//   * done() flips true only after the final Quit; failed() if the
//     deadline expires or an observation mismatch is detected.

class EditorSdkWorkflow {
    // Stable-Y window before pressing J. Each pre-jump site owns its
    // own state so a fresh baseline is recorded per session.
    struct StableYState {
        std::uint64_t start_version = 0;
        std::uint64_t last_version = 0;
        double baseline = 0;
        std::uint64_t start_tick = 0;
    };
    unsigned stage_ = 0;
    Uint64 started_ = SDL_GetTicks();
    Uint64 stage_started_ = 0;
    double peak_y_ = 0;
    std::uint64_t jump_last_version_ = 0;
    StableYState pre_resume_jump_{};
    StableYState pre_continue_jump_{};
    StableYState pre_restart_continue_jump_{};
    bool held_w_ = false;
    bool failed_ = false;
    bool done_ = false;
    bool captured_failure_ = false;
    // Outside-panel surrender substates. Drives the focus-gated
    // routing regression through Escape → pause → split-click on
    // an observed Hierarchy control (does NOT replace the Game
    // panel) → wait for click to FINISH (UP sent) + at least one
    // subsequent editor frame to process the queued UP → record
    // snapshot baseline → assert !captured → record Rml focus →
    // send Tab → wait at least one editor frame to process the
    // queued Tab → record snapshot baseline → wait for snapshot
    // advance → assert focus UNCHANGED + routing off → capture
    // sdk-outside-surrender → arm click on Capture gameplay input
    // → wait click finished + frame → record baseline → assert
    // captured → record focus → send Tab → wait frame → record
    // baseline → wait snapshot advance → assert focus CHANGED →
    // capture sdk-outside-regain → navigate to Resume via the
    // existing activate() helper → tap E. Each substate gates on
    // a different page / focus mode / click-finished / frame
    // counter / snapshot version so the parent stage 2 advance is
    // decoupled from the focus-mode transition AND from
    // pre-processing snapshot noise. The previous-frame focus bool
    // is never trusted; the regression only progresses after the
    // runtime has actually processed the queued SDL events and
    // shipped a fresh snapshot. See below.
    unsigned outside_phase_ = 0; // 0=initial gameplay, 1=paused+armed
                                 //  Hierarchy, 2=paused+click finished,
                                 // 3=paused+Tab processed, 4=paused+armed
                                 //  Capture, 5=paused+click finished,
                                 // 6=paused+Tab processed, 7=Resume nav,
                                 // 8=play restored
    static constexpr Uint64 outside_phase_deadline_ms_ = 4000;
    // Persistent phase progress. snapshot baseline is recorded
    // AFTER the click / Tab has actually been processed by the
    // editor, never at ARM time. outside_ack_frames_ counts
    // editor frames since the last ARM or tap_key; when it
    // reaches the per-action threshold, the baseline is recorded
    // and the phase advances.
    std::uint64_t outside_phase_version_ = 0; // snapshot version
                                              // recorded AFTER
                                              // click/Tab is
                                              // processed; phase
                                              // must wait for an
                                              // advance before any
                                              // assertion
    Uint64 outside_phase_started_ = 0;        // SDL tick at phase start;
                                              // bounded deadline before the
                                              // phase is declared stuck
    unsigned outside_ack_frames_ = 0;         // editor frames since the
                                              // last ARM or tap_key
    Rml::Element* outside_focus_pre_ = nullptr;
    std::string outside_focus_label_pre_;
    bool outside_focus_recorded_ = false;
    bool outside_click_pending_ = false;
    bool outside_click_armed_ = false;
    float outside_click_x_ = 0;
    float outside_click_y_ = 0;

    // Save baseline recorded before Escape+Save Game.
    double initial_z_ = 0;           // player z before W movement
    double save_z_ = 0;              // player z at Save time
    int save_interactions_ = 0;      // model.interactions at Save time
    std::string save_session_;       // play.session() at Save time
    std::uint64_t save_version_ = 0; // snapshot_version at Save time
    std::uint64_t save_generation_ = 0;
    std::string save_asset_id_; // effective_snapshot.asset_id at Save

    // Jump baseline: y before the press; assertion is a relative
    // rise so a fall from an elevated position cannot satisfy it.
    double jump_baseline_y_ = 0;
    bool jump_baseline_recorded_ = false;
    double y_after_continue_ = 0;

    // Asset-id / generation transition tracking.
    std::uint64_t pre_menu_generation_ = 0;
    std::string pre_menu_session_;
    std::string post_restart_session_;

    std::filesystem::path evidence_;
    std::string source_commit_;
    std::string build_id_;
    Json trace_ = Json::object();
    Json failure_ = Json::object();
    CaptureFn capture_;
    CaptureFn failure_capture_;
    std::set<std::string> captures_;

    // Per-frame toolbar click staged across two frames. arm is
    // idempotent while already armed; stage 0 calls it every frame
    // until Play is active, so re-arming while armed would re-queue
    // DOWN before UP and never complete the click.
    bool toolbar_down_pending_ = false;
    float toolbar_x_ = 0;
    float toolbar_y_ = 0;
    bool toolbar_armed_ = false;
    bool toolbar_click_attempted_ = false;

    static void push_event(SDL_Event event) {
        if (!SDL_PushEvent(&event))
            throw std::runtime_error(SDL_GetError());
    }
    static void key_down(SDL_Window* window, SDL_Scancode code, SDL_Keycode value) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(window);
        event.key.scancode = code;
        event.key.key = value;
        event.key.down = true;
        push_event(event);
    }
    static void key_up(SDL_Window* window, SDL_Scancode code, SDL_Keycode value) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window);
        event.key.scancode = code;
        event.key.key = value;
        event.key.down = false;
        push_event(event);
    }
    static void tap_key(SDL_Window* window, SDL_Scancode code, SDL_Keycode value) {
        key_down(window, code, value);
        key_up(window, code, value);
    }
    static void mouse_motion(SDL_Window* window, float x, float y) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(window);
        event.motion.x = x;
        event.motion.y = y;
        push_event(event);
    }
    static void mouse_down(SDL_Window* window, float x, float y) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = SDL_GetWindowID(window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
        event.button.down = true;
        push_event(event);
    }
    static void mouse_up(SDL_Window* window, float x, float y) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = SDL_GetWindowID(window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
        event.button.down = false;
        push_event(event);
    }

    // Read-only observer for the existing input owner's logical routing
    // state. main.cpp wires this to `game_input.captured()` so the
    // workflow never reaches across the ownership boundary. The
    // observer is fixture-only (no new input authority, no protocol
    // field added to PlaySession). Returns false when the observer
    // has not been wired so a fresh workflow cannot satisfy the
    // regression by accident.
    std::function<bool()> game_input_captured_observer_;
    void set_game_input_observer(std::function<bool()> fn) {
        game_input_captured_observer_ = std::move(fn);
    }
    bool game_input_captured() const {
        return game_input_captured_observer_ ? game_input_captured_observer_() : false;
    }

    // Per-scope split-click pattern, mirrored from the toolbar
    // arm/pump pair. arm_outside_click() queues DOWN for the next
    // pump; pump_outside_click() sends DOWN on the first call and
    // UP on the second, completing a real two-frame click. This is
    // the documented click sequence the live editor processes; a
    // single-frame mouse_down + mouse_up is rejected by SDL because
    // ImGui consumes motion-only events before the button up is
    // delivered.
    void arm_outside_click(const ImVec2& center) {
        if (outside_click_armed_)
            return;
        outside_click_pending_ = true;
        outside_click_armed_ = true;
        outside_click_x_ = center.x;
        outside_click_y_ = center.y;
    }
    void pump_outside_click(SDL_Window* window) {
        if (!outside_click_armed_)
            return;
        if (outside_click_pending_) {
            mouse_motion(window, outside_click_x_, outside_click_y_);
            mouse_down(window, outside_click_x_, outside_click_y_);
            outside_click_pending_ = false;
            return;
        }
        mouse_up(window, outside_click_x_, outside_click_y_);
        outside_click_armed_ = false;
    }

    // Live observations from the running editor + PlaySession.
    static Json ui_model(PlaySession& play) {
        const auto snapshot = play.ui_snapshot();
        if (!snapshot.is_object() || !snapshot.contains("documents"))
            return Json::object();
        for (const auto& doc : snapshot.at("documents")) {
            if (!doc.is_object())
                continue;
            if (doc.value("entity", "") != reference::ui_id)
                continue;
            if (!doc.value("visible", false))
                continue;
            return doc.value("model", Json::object());
        }
        return Json::object();
    }
    static std::string model_page(const Json& model) { return model.value("page", ""); }
    static int model_interactions(const Json& model) { return model.value("interactions", 0); }
    static std::string model_message(const Json& model) { return model.value("message", ""); }
    static std::string model_binding(const Json& model) { return model.value("binding", ""); }

    static std::array<double, 3> player_position(PlaySession& play) {
        const auto scene = play.effective_snapshot();
        if (!scene.is_object())
            return {0, 0, 0};
        for (const auto& e : scene.value("entities", Json::array())) {
            if (!e.is_object() || e.value("id", "") != reference::player_id)
                continue;
            const auto t = e.value("components", Json::object())
                               .value("forge.local_translation", Json::object());
            return {t.value("x", 0.0), t.value("y", 0.0), t.value("z", 0.0)};
        }
        return {0, 0, 0};
    }
    static std::string scene_asset(PlaySession& play) {
        return play.effective_snapshot().value("asset_id", "");
    }

    // Locate the live visible Rml context: the context that owns
    // exactly one visible document. Returns nullptr when staged /
    // hidden / not-yet-published trees are still in the system.
    static Rml::Context* live_visible_context() {
        Rml::Context* match = nullptr;
        const auto count = Rml::GetNumContexts();
        for (int i = 0; i < count; ++i) {
            auto* ctx = Rml::GetContext(i);
            if (!ctx)
                continue;
            int visible = 0;
            for (int j = 0; j < ctx->GetNumDocuments(); ++j) {
                auto* doc = ctx->GetDocument(j);
                if (doc && doc->IsVisible())
                    ++visible;
            }
            if (visible == 1) {
                if (match)
                    return nullptr; // ambiguous
                match = ctx;
            }
        }
        return match;
    }

    // Native Rml activation: required visible live context/document,
    // real focus tree, return on label match else tab. Mirrors the
    // proven reference_game_workflow.hpp:59-77 activate() helper.
    static bool activate(SDL_Window* window, const std::string& label) {
        auto* context = live_visible_context();
        if (!context)
            return false;
        auto* focus = context->GetFocusElement();
        if (focus && focus->IsVisible(true) && focus->GetTagName() == "button" &&
            focus->GetInnerRML() == label) {
            tap_key(window, SDL_SCANCODE_RETURN, SDLK_RETURN);
            return true;
        }
        tap_key(window, SDL_SCANCODE_TAB, SDLK_TAB);
        return false;
    }

    static bool toolbar_play_visible(ImVec2& center) {
        const auto it = ui_targets.find("icon:play");
        if (it == ui_targets.end())
            return false;
        const auto w = it->second.maximum.x - it->second.minimum.x;
        const auto h = it->second.maximum.y - it->second.minimum.y;
        const bool clipped = w > 0 && h > 0 && it->second.minimum.x >= it->second.clip_minimum.x &&
                             it->second.minimum.y >= it->second.clip_minimum.y &&
                             it->second.maximum.x <= it->second.clip_maximum.x &&
                             it->second.maximum.y <= it->second.clip_maximum.y;
        if (!it->second.enabled || !clipped)
            return false;
        center.x = (it->second.minimum.x + it->second.maximum.x) * 0.5f;
        center.y = (it->second.minimum.y + it->second.maximum.y) * 0.5f;
        return true;
    }

    static void require(bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    }
    // Per-scope stable-Y helper. start_version/last_version guard
    // against accumulating proof on stale cache reads; the start tick
    // is recorded on the first snapshot, advanced only when y drifts
    // beyond tolerance, and read against the runtime clock so a
    // paused/stalled runtime does not advance the proof.
    static bool stable_y_settled(StableYState& state, double y, std::uint64_t version,
                                 const Json& timing, double tolerance = 1e-3,
                                 double min_seconds = 0.5) {
        const auto tick = timing.value("tick", std::uint64_t{});
        const auto fixed_dt = timing.value("fixed_dt", 1.0 / 60.0);
        if (state.start_version == 0) {
            state.start_version = version;
            state.last_version = version;
            state.baseline = y;
            state.start_tick = tick;
            return false;
        }
        if (version == state.last_version)
            return false;
        state.last_version = version;
        if (std::abs(y - state.baseline) > tolerance) {
            state.baseline = y;
            state.start_tick = tick;
            return false;
        }
        return double(tick - state.start_tick) * fixed_dt >= min_seconds;
    }

    void arm_toolbar_click(const ImVec2& center) {
        if (toolbar_armed_)
            return;
        toolbar_down_pending_ = true;
        toolbar_armed_ = true;
        toolbar_x_ = center.x;
        toolbar_y_ = center.y;
    }
    void pump_toolbar_click(SDL_Window* window) {
        if (!toolbar_armed_)
            return;
        if (toolbar_down_pending_) {
            mouse_motion(window, toolbar_x_, toolbar_y_);
            mouse_down(window, toolbar_x_, toolbar_y_);
            toolbar_down_pending_ = false;
            return;
        }
        mouse_up(window, toolbar_x_, toolbar_y_);
        toolbar_armed_ = false;
        toolbar_click_attempted_ = true;
    }

    void append_stage(unsigned next, PlaySession& play, const std::string& note) {
        if (!trace_.contains("stages") || !trace_["stages"].is_array())
            trace_["stages"] = Json::array();
        Json entry = {{"from", stage_}, {"to", next}, {"note", note}};
        const auto model = ui_model(play);
        entry["page"] = model_page(model);
        entry["interactions"] = model_interactions(model);
        entry["binding"] = model_binding(model);
        entry["asset_id"] = scene_asset(play);
        entry["session"] = play.session();
        entry["snapshot_version"] = play.snapshot_version();
        entry["activation_generation"] = play.sdk_activation_generation();
        entry["status"] = play.status();
        trace_["stages"].push_back(std::move(entry));
        stage_ = next;
        stage_started_ = SDL_GetTicks();
    }

    void record_failure(const std::string& why, PlaySession& play) {
        if (failed_)
            return;
        failed_ = true;
        const auto model = ui_model(play);
        failure_ = {{"stage", stage_},
                    {"reason", why},
                    {"status", play.status()},
                    {"page", model_page(model)},
                    {"binding", model_binding(model)},
                    {"message", model_message(model)},
                    {"interactions", model_interactions(model)},
                    {"asset_id", scene_asset(play)},
                    {"session", play.session()},
                    {"snapshot_version", play.snapshot_version()},
                    {"activation_generation", play.sdk_activation_generation()},
                    {"position_z", player_position(play)[2]},
                    {"save_z", save_z_},
                    {"save_interactions", save_interactions_}};
        trace_["failure"] = failure_;
        trace_["complete"] = false;
        if (failure_capture_ && !captured_failure_) {
            captured_failure_ = true;
            failure_capture_("sdk-workflow-failure", trace_);
        }
    }

    void capture(const char* name, PlaySession& play) {
        if (!captures_.insert(name).second)
            return;
        if (capture_)
            capture_(name, trace_);
        (void)play;
    }

    void release_held(SDL_Window* window) {
        if (held_w_) {
            key_up(window, SDL_SCANCODE_W, SDLK_W);
            held_w_ = false;
        }
        if (toolbar_armed_)
            pump_toolbar_click(window);
    }

  public:
    explicit EditorSdkWorkflow(std::filesystem::path evidence) : evidence_(std::move(evidence)) {
        source_commit_ = forge::source_commit ? forge::source_commit : "";
        build_id_ = forge::build_id ? forge::build_id : "";
        observe_ui = true;
        trace_["workflow"] = "editor-sdk";
        trace_["source_commit"] = source_commit_;
        trace_["build_id"] = build_id_;
        trace_["stages"] = Json::array();
    }
    ~EditorSdkWorkflow() { observe_ui = false; }

    void set_capture(CaptureFn fn) { capture_ = std::move(fn); }
    void set_failure_capture(CaptureFn fn) { failure_capture_ = std::move(fn); }

    bool done() const { return done_; }
    bool failed() const { return failed_; }
    const Json& trace() const { return trace_; }
    const Json& failure() const { return failure_; }

    // Per-frame: invoked once per editor frame at the input/render point,
    // BEFORE SDL_PollEvent. `has_staged` reflects PlayPresentation.has_staged();
    // when true the runtime Rml is not the live owner yet and the workflow
    // gates native Rml input to avoid driving a staged tree.
    void frame(PlaySession& play, SDL_Window* window, bool has_staged) {
        if (done_ || failed_) {
            // Editor left usable: drain any pending toolbar click up event
            // and release any held key on terminal state.
            release_held(window);
            return;
        }
        try {
            frame_impl(play, window, has_staged);
        } catch (...) {
            release_held(window);
            throw;
        }
    }

    void frame_impl(PlaySession& play, SDL_Window* window, bool has_staged) {
        // Overall + per-stage deadlines.
        const auto now = SDL_GetTicks();
        if (now - started_ > kOverallBudgetMs) {
            record_failure("workflow timed out", play);
            release_held(window);
            return;
        }
        if (stage_started_ == 0)
            stage_started_ = now;
        if (now - stage_started_ > kStageBudgetMs) {
            record_failure("stage deadline exceeded", play);
            release_held(window);
            return;
        }
        // Pump staged toolbar click first so its second-frame up is observed.
        pump_toolbar_click(window);
        // Same for the outside-surrender split-click: DOWN is sent
        // on the first frame after arm, UP on the next. Pumping at
        // the top of the frame keeps each click a single render
        // tick long so it reaches ImGui as a real press.
        pump_outside_click(window);

        const auto model = ui_model(play);
        const auto page = model_page(model);
        const auto rel_mouse = SDL_GetWindowRelativeMouseMode(window);

        // Gate native Rml input: do not drive when the live context is
        // staged (not the published live document) or when no unique
        // visible live document is observable.
        const bool live_rml = !has_staged && live_visible_context() != nullptr;

        // Stage 0: click toolbar Play, wait for runtime to become active
        // and publish the main menu document. One click attempt; the
        // arm is idempotent so re-entering stage 0 while !play.active
        // does not re-queue DOWN before UP.
        if (stage_ == 0) {
            if (!play.active()) {
                if (!toolbar_click_attempted_) {
                    ImVec2 center{};
                    if (toolbar_play_visible(center))
                        arm_toolbar_click(center);
                }
                return;
            }
            capture("sdk-play-active", play);
            append_stage(1, play, "toolbar play");
            return;
        }
        if (stage_ == 1 && page == "main" && live_rml) {
            capture("sdk-main-menu", play);
            if (activate(window, "New Game"))
                append_stage(2, play, "new game");
            return;
        }
        if (stage_ == 2 && outside_phase_ == 0 && page == "play" && live_rml &&
            scene_asset(play) == reference::level_scene && rel_mouse) {
            // Initial gameplay reached. Capture the gameplay
            // screenshot, then enter Escape so the rest of the
            // regression proceeds in real pointer mode (the live
            // editor leaves relative-mouse mode the moment the
            // pause menu opens). Doing the surrender from a
            // captured relative-mode state is not a normal pointer
            // UI surrender — the documented user path is Escape to
            // pause, then click outside the Game panel. The
            // snapshot baseline is NOT recorded here: it is
            // recorded AFTER the next click finishes so the
            // baseline reflects the state the runtime actually
            // observed, not the pre-click ARM state.
            capture("sdk-gameplay", play);
            tap_key(window, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
            outside_phase_ = 1;
            outside_phase_version_ = 0;
            outside_phase_started_ = SDL_GetTicks();
            outside_ack_frames_ = 0;
            return;
        }
        // Phase 1: pause menu open, pointer mode on. Arm a real
        // two-frame click on an observed Hierarchy control. The
        // Hierarchy panel is visible in the default workspace
        // layout alongside the Game tab and the click does NOT
        // replace Game with another tab. Default layout puts Scene
        // + Game as sibling tabs, so clicking tab:Scene would hide
        // Game and break the regression — the user explicitly
        // required an editor control that keeps Game visible.
        // The click is queued via arm_outside_click(); the actual
        // DOWN is sent on the NEXT frame's pump, and the actual
        // UP is sent on the FRAME AFTER THAT. The baseline is
        // recorded AFTER pump fires both events and the editor
        // has had a chance to process the queued UP.
        if (stage_ == 2 && outside_phase_ == 1 && page == "pause" && live_rml && !rel_mouse) {
            const auto hierarchy_btn = ui_targets.find("hierarchy:expand-all");
            require(hierarchy_btn != ui_targets.end() && hierarchy_btn->second.enabled,
                    "Hierarchy expand-all control probe missing or disabled; "
                    "the surrender regression requires an editor control that "
                    "stays visible alongside the Game panel in default layout");
            outside_click_x_ =
                (hierarchy_btn->second.minimum.x + hierarchy_btn->second.maximum.x) * 0.5f;
            outside_click_y_ =
                (hierarchy_btn->second.minimum.y + hierarchy_btn->second.maximum.y) * 0.5f;
            arm_outside_click({outside_click_x_, outside_click_y_});
            outside_phase_ = 2;
            outside_phase_version_ = 0;
            outside_phase_started_ = SDL_GetTicks();
            outside_ack_frames_ = 0;
            outside_focus_recorded_ = false;
            return;
        }
        // Phase 2: wait for the click to be FINISHED (UP was sent
        // on a previous frame's pump) AND at least 2 more editor
        // frames to pass so the runtime has actually consumed
        // the queued SDL events. pump_outside_click() runs at the
        // top of frame_impl exactly once per editor frame, so
        // this block does NOT re-pump. The snapshot baseline is
        // recorded AFTER these waits so the baseline reflects
        // post-click state, never the pre-click ARM state.
        if (stage_ == 2 && outside_phase_ == 2 && page == "pause" && live_rml && !rel_mouse) {
            const auto now = SDL_GetTicks();
            if (outside_click_armed_) {
                outside_ack_frames_ = 0;
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure("Phase 2: click never finished (UP not sent) within deadline",
                                   play);
                    return;
                }
                return;
            }
            outside_ack_frames_++;
            // 2 frames after click finished: 1 for the editor to
            // process the queued UP, 1 for the runtime to publish
            // the next snapshot the assertion will use as its
            // baseline. Counting editor frames rather than wall
            // time makes the regression deterministic across hosts.
            if (outside_ack_frames_ < 3) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 2: editor did not advance frames after click within deadline", play);
                    return;
                }
                return;
            }
            require(!game_input_captured(),
                    "Game still captured after pause + outside Hierarchy click");
            // Snapshot baseline recorded AFTER the click is fully
            // processed. Subsequent phase 3 will wait for this
            // baseline to advance before any Tab assertion fires.
            outside_phase_version_ = play.snapshot_version();
            outside_phase_started_ = SDL_GetTicks();
            // Record the current Rml focus element + its label so
            // the post-Tab assertion can compare. The focus
            // element + inner RML string is the documented Rml
            // identity; visible() + tag=="button" filters out
            // container / hidden elements.
            auto* ctx = live_visible_context();
            outside_focus_pre_ = ctx ? ctx->GetFocusElement() : nullptr;
            outside_focus_label_pre_ = outside_focus_pre_ && outside_focus_pre_->IsVisible(true) &&
                                               outside_focus_pre_->GetTagName() == "button"
                                           ? std::string(outside_focus_pre_->GetInnerRML())
                                           : std::string{};
            outside_focus_recorded_ = true;
            // Send Tab through real SDL. activate()'s Tab is the
            // same path; using it here keeps the navigation key
            // observable to the same downstream Rml pipeline.
            tap_key(window, SDL_SCANCODE_TAB, SDLK_TAB);
            outside_ack_frames_ = 0;
            outside_phase_ = 3;
            return;
        }
        // Phase 3: wait at least 2 editor frames so the runtime
        // has processed the queued Tab key, then record a fresh
        // snapshot baseline, then wait for the snapshot_version
        // to actually advance before the assertion. Snapshot
        // advance alone is insufficient evidence the Tab was
        // processed — unrelated snapshot activity can advance the
        // counter. The minimum 2-frame wait after tap_key is the
        // ordering guarantee that this frame's queued SDL key
        // has been processed by the runtime.
        if (stage_ == 2 && outside_phase_ == 3 && page == "pause" && live_rml && !rel_mouse) {
            const auto now = SDL_GetTicks();
            outside_ack_frames_++;
            if (outside_ack_frames_ < 3) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 3: editor did not advance frames after Tab within deadline", play);
                    return;
                }
                return;
            }
            const auto baseline = outside_phase_version_;
            const auto advanced = play.snapshot_version() > baseline;
            if (!advanced) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 3: snapshot_version did not advance after Tab within deadline",
                        play);
                    return;
                }
                return;
            }
            require(!game_input_captured(), "Routing re-engaged before surrender proof completed");
            require(outside_focus_recorded_,
                    "Phase 3: pre-Tab focus was not recorded (phase 2 skipped)");
            auto* ctx = live_visible_context();
            auto* focus_post = ctx ? ctx->GetFocusElement() : nullptr;
            const std::string focus_label_post =
                focus_post && focus_post->IsVisible(true) && focus_post->GetTagName() == "button"
                    ? std::string(focus_post->GetInnerRML())
                    : std::string{};
            require(focus_label_post == outside_focus_label_pre_,
                    "Tab key navigated Rml focus despite surrender; routing not actually "
                    "suppressing runtime menu keys");
            capture("sdk-outside-surrender", play);
            outside_phase_ = 4;
            outside_phase_version_ = 0;
            outside_phase_started_ = SDL_GetTicks();
            outside_ack_frames_ = 0;
            return;
        }
        // Phase 4: arm a real two-frame click on the actual
        // Capture gameplay input button the live pause menu
        // exposes. Clicking tab:Game while paused does NOT
        // re-engage relative mouse mode and does NOT enable menu
        // navigation; only the Capture button does.
        if (stage_ == 2 && outside_phase_ == 4 && page == "pause" && live_rml && !rel_mouse) {
            const auto capture_btn = ui_targets.find("button:Capture gameplay input");
            require(capture_btn != ui_targets.end() && capture_btn->second.enabled,
                    "Capture gameplay input button probe missing or disabled");
            outside_click_x_ =
                (capture_btn->second.minimum.x + capture_btn->second.maximum.x) * 0.5f;
            outside_click_y_ =
                (capture_btn->second.minimum.y + capture_btn->second.maximum.y) * 0.5f;
            arm_outside_click({outside_click_x_, outside_click_y_});
            outside_phase_ = 5;
            outside_phase_version_ = 0;
            outside_phase_started_ = SDL_GetTicks();
            outside_ack_frames_ = 0;
            return;
        }
        // Phase 5: same wait-for-click-finished + 2 frame pattern
        // as phase 2, but assert captured==true after the click.
        // Recording the baseline AFTER the click is fully
        // processed is what prevents the premature fail the user
        // flagged in v3.
        if (stage_ == 2 && outside_phase_ == 5 && page == "pause" && live_rml && !rel_mouse) {
            const auto now = SDL_GetTicks();
            if (outside_click_armed_) {
                outside_ack_frames_ = 0;
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 5: Capture-button click never finished (UP not sent) within "
                        "deadline",
                        play);
                    return;
                }
                return;
            }
            outside_ack_frames_++;
            if (outside_ack_frames_ < 3) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 5: editor did not advance frames after Capture click within "
                        "deadline",
                        play);
                    return;
                }
                return;
            }
            require(game_input_captured(),
                    "Capture gameplay input did not re-engage logical routing");
            outside_phase_version_ = play.snapshot_version();
            outside_phase_started_ = SDL_GetTicks();
            auto* ctx = live_visible_context();
            outside_focus_pre_ = ctx ? ctx->GetFocusElement() : nullptr;
            outside_focus_label_pre_ = outside_focus_pre_ && outside_focus_pre_->IsVisible(true) &&
                                               outside_focus_pre_->GetTagName() == "button"
                                           ? std::string(outside_focus_pre_->GetInnerRML())
                                           : std::string{};
            outside_focus_recorded_ = true;
            tap_key(window, SDL_SCANCODE_TAB, SDLK_TAB);
            outside_ack_frames_ = 0;
            outside_phase_ = 6;
            return;
        }
        // Phase 6: same Tab-processed-wait + baseline + advance
        // pattern as phase 3. The frame counter is reset only on
        // tap_key (not at the start of the phase), so the counter
        // measures frames since the queued Tab was sent.
        if (stage_ == 2 && outside_phase_ == 6 && page == "pause" && live_rml && !rel_mouse) {
            const auto now = SDL_GetTicks();
            outside_ack_frames_++;
            if (outside_ack_frames_ < 3) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 6: editor did not advance frames after post-regain Tab within "
                        "deadline",
                        play);
                    return;
                }
                return;
            }
            const auto baseline = outside_phase_version_;
            const auto advanced = play.snapshot_version() > baseline;
            if (!advanced) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 6: snapshot_version did not advance after post-regain Tab within "
                        "deadline",
                        play);
                    return;
                }
                return;
            }
            require(outside_focus_recorded_,
                    "Phase 6: pre-Tab focus was not recorded (phase 5 skipped)");
            auto* ctx = live_visible_context();
            auto* focus_post = ctx ? ctx->GetFocusElement() : nullptr;
            const std::string focus_label_post =
                focus_post && focus_post->IsVisible(true) && focus_post->GetTagName() == "button"
                    ? std::string(focus_post->GetInnerRML())
                    : std::string{};
            require(focus_label_post != outside_focus_label_pre_,
                    "Tab key did not navigate menu after focus regained; runtime menu keys "
                    "still suppressed");
            capture("sdk-outside-regain", play);
            outside_phase_ = 7;
            outside_phase_version_ = 0;
            outside_phase_started_ = SDL_GetTicks();
            outside_ack_frames_ = 0;
            return;
        }
        // Phase 7: navigate to Resume via the existing
        // activate() helper so a focused Resume selection exists
        // before the final resume command, then send Enter. The
        // activate() helper is the proven reference path that
        // walks the Rml focus tree until it lands on the named
        // button — using it here means the focus_gated_routing
        // trace flag only fires after a real Resume selection
        // exists, not a guessed one.
        if (stage_ == 2 && outside_phase_ == 7 && page == "pause" && live_rml && !rel_mouse) {
            const auto now = SDL_GetTicks();
            if (!activate(window, "Resume")) {
                // activate() returns false when it had to send
                // another Tab to walk forward — wait a frame for
                // the menu to settle and try again.
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure("Phase 7: Resume navigation timed out before focus landed",
                                   play);
                    return;
                }
                return;
            }
            tap_key(window, SDL_SCANCODE_RETURN, SDLK_RETURN);
            // Advance to phase 8; phase 8 waits for page==play
            // and the runtime to actually process the Resume
            // command before the tap E fires.
            outside_phase_ = 8;
            outside_phase_version_ = 0;
            outside_phase_started_ = SDL_GetTicks();
            outside_ack_frames_ = 0;
            return;
        }
        // Phase 8: wait at least 2 editor frames after the
        // Resume Enter so the runtime has processed the resume
        // command and published a fresh snapshot, then verify
        // page==play + rel_mouse + level_scene before tapping
        // E to advance to the existing stage 3 beacon
        // interaction. Recording the snapshot baseline AFTER the
        // Enter ensures the baseline reflects post-Resume state.
        if (stage_ == 2 && outside_phase_ == 8 && page == "play" && live_rml &&
            scene_asset(play) == reference::level_scene && rel_mouse) {
            const auto now = SDL_GetTicks();
            outside_ack_frames_++;
            if (outside_ack_frames_ < 3) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 8: editor did not advance frames after Resume within deadline",
                        play);
                    return;
                }
                return;
            }
            const auto baseline = outside_phase_version_;
            const auto advanced = play.snapshot_version() > baseline;
            if (!advanced) {
                if (now - outside_phase_started_ > outside_phase_deadline_ms_) {
                    record_failure(
                        "Phase 8: snapshot_version did not advance after Resume within deadline",
                        play);
                    return;
                }
                return;
            }
            tap_key(window, SDL_SCANCODE_E, SDLK_E);
            append_stage(3, play, "E pressed");
            return;
        }
        if (stage_ == 3 && model_interactions(model) == 1 && live_rml) {
            require(!model.value("prompt", "").empty(),
                    "Beacon interaction had no player-facing prompt");
            capture("sdk-interaction", play);
            initial_z_ = player_position(play)[2];
            mouse_motion(window, 0, 0);
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.xrel = 10;
            motion.motion.yrel = -2;
            push_event(motion);
            key_down(window, SDL_SCANCODE_W, SDLK_W);
            held_w_ = true;
            append_stage(4, play, "moving forward");
            return;
        }
        if (stage_ == 4) {
            const auto z = player_position(play)[2];
            if (z > initial_z_ + 0.5) {
                key_up(window, SDL_SCANCODE_W, SDLK_W);
                held_w_ = false;
                capture("sdk-moved", play);
                tap_key(window, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
                append_stage(5, play, "paused");
                return;
            }
            return;
        }
        if (stage_ == 5 && page == "pause" && live_rml) {
            capture("sdk-pause", play);
            if (activate(window, "Options"))
                append_stage(6, play, "options");
            return;
        }
        if (stage_ == 6 && page == "options" && live_rml) {
            if (activate(window, "Rebind Jump"))
                append_stage(7, play, "rebind");
            return;
        }
        if (stage_ == 7 && model_binding(model).rfind("Press a key", 0) == 0 && live_rml) {
            capture("sdk-listening", play);
            tap_key(window, SDL_SCANCODE_J, SDLK_J);
            append_stage(8, play, "J pressed");
            return;
        }
        if (stage_ == 8 && model_binding(model) == "key.j" && live_rml) {
            capture("sdk-candidate", play);
            if (activate(window, "Apply binding"))
                append_stage(9, play, "apply");
            return;
        }
        if (stage_ == 9 && model_binding(model) != "key.j" && live_rml) {
            capture("sdk-applied", play);
            if (activate(window, "Back"))
                append_stage(10, play, "back");
            return;
        }
        if (stage_ == 10 && page == "pause" && live_rml) {
            if (activate(window, "Resume"))
                append_stage(11, play, "resume");
            return;
        }
        if (stage_ == 11 && page == "play" && rel_mouse && live_rml) {
            if (!stable_y_settled(pre_resume_jump_, player_position(play)[1],
                                  play.snapshot_version(), play.timing()))
                return;
            jump_baseline_y_ = player_position(play)[1];
            jump_baseline_recorded_ = false;
            tap_key(window, SDL_SCANCODE_J, SDLK_J);
            append_stage(12, play, "rebound jump");
            return;
        }
        if (stage_ == 12) {
            const auto y = player_position(play)[1];
            if (!jump_baseline_recorded_) {
                // Wait for the rise to peak: y > baseline + 0.15.
                if (y > jump_baseline_y_ + 0.15) {
                    jump_baseline_recorded_ = true;
                    peak_y_ = y;
                }
                return;
            }
            // Track the peak; require a fresh snapshot version between
            // observations so we are not comparing a stale cache value.
            const auto version = play.snapshot_version();
            if (version != jump_last_version_) {
                if (y > peak_y_)
                    peak_y_ = y;
                if (y > jump_baseline_y_ + 0.05) {
                    // Still airborne; keep waiting for landing.
                    jump_last_version_ = version;
                    return;
                }
                // Landed: y returned to baseline within tolerance.
                if (std::abs(y - jump_baseline_y_) > 1e-2) {
                    // Close to ground but not settled; keep observing.
                    jump_last_version_ = version;
                    return;
                }
                capture("sdk-rebound-jump", play);
                tap_key(window, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
                append_stage(13, play, "paused for save");
                return;
            }
            return;
        }
        if (stage_ == 13 && page == "pause" && live_rml) {
            if (activate(window, "Save Game"))
                append_stage(14, play, "save");
            return;
        }
        if (stage_ == 14 && model_message(model) == "Game saved" && live_rml) {
            capture("sdk-saved", play);
            // Persist the assertable baseline. After Continue the
            // loaded scene must reproduce interactions == 1 and a
            // non-default z close to save_z_ (within the same process;
            // a fresh process restart asserts save_z_ vs the live
            // position too).
            save_interactions_ = model_interactions(model);
            save_z_ = player_position(play)[2];
            save_session_ = play.session();
            save_version_ = play.snapshot_version();
            save_generation_ = play.sdk_activation_generation();
            save_asset_id_ = scene_asset(play);
            if (save_interactions_ != 1) {
                record_failure("Save baseline had wrong interactions", play);
                return;
            }
            if (save_z_ <= initial_z_ + 0.5) {
                record_failure("Save baseline z did not reflect W movement", play);
                return;
            }
            if (save_asset_id_ != reference::level_scene) {
                record_failure("Save baseline was not on level scene", play);
                return;
            }
            forge::atomic_write(evidence_ / "sdk-save-state.json",
                                Json{{"interactions", save_interactions_},
                                     {"position_z", save_z_},
                                     {"session", save_session_},
                                     {"snapshot_version", save_version_},
                                     {"generation", save_generation_},
                                     {"asset_id", save_asset_id_}}
                                    .dump(2));
            if (activate(window, "Main Menu")) {
                pre_menu_generation_ = save_generation_;
                pre_menu_session_ = save_session_;
                append_stage(15, play, "menu requested");
            }
            return;
        }
        if (stage_ == 15 && page == "main" && live_rml) {
            const auto asset = scene_asset(play);
            const auto gen = play.sdk_activation_generation();
            const auto sess = play.session();
            // Asset must change to menu AND the activation generation or
            // session must advance to prove the runtime actually transitioned.
            const bool asset_changed = asset == reference::menu_scene;
            const bool session_changed = !sess.empty() && sess != pre_menu_session_;
            const bool generation_changed = gen != pre_menu_generation_;
            if (!asset_changed)
                return;
            if (!session_changed && !generation_changed)
                return;
            capture("sdk-main-returned", play);
            if (activate(window, "Continue"))
                append_stage(16, play, "continue");
            return;
        }
        if (stage_ == 16 && page == "play" && live_rml &&
            scene_asset(play) == reference::level_scene) {
            // Same-process Continue must restore interactions and a
            // non-default z matching the Save baseline.
            if (model_interactions(model) != 1)
                return;
            const auto z = player_position(play)[2];
            if (std::abs(z - save_z_) > 0.15)
                return;
            capture("sdk-restored", play);
            // Persisted binding proof: wait for stable Y on the
            // freshly loaded scene, then press J and require a real
            // rise from baseline.
            if (!stable_y_settled(pre_continue_jump_, player_position(play)[1],
                                  play.snapshot_version(), play.timing()))
                return;
            jump_baseline_y_ = player_position(play)[1];
            jump_baseline_recorded_ = false;
            tap_key(window, SDL_SCANCODE_J, SDLK_J);
            append_stage(17, play, "post-restart jump");
            return;
        }
        if (stage_ == 17) {
            const auto y = player_position(play)[1];
            if (!jump_baseline_recorded_) {
                if (y > jump_baseline_y_ + 0.15) {
                    jump_baseline_recorded_ = true;
                    peak_y_ = y;
                }
                return;
            }
            const auto version = play.snapshot_version();
            if (version != jump_last_version_) {
                if (y > peak_y_)
                    peak_y_ = y;
                if (y > jump_baseline_y_ + 0.05) {
                    jump_last_version_ = version;
                    return;
                }
                if (std::abs(y - jump_baseline_y_) > 1e-2) {
                    jump_last_version_ = version;
                    return;
                }
                capture("sdk-persisted-binding", play);
                tap_key(window, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
                append_stage(18, play, "paused after persisted binding");
                return;
            }
            return;
        }
        if (stage_ == 18 && page == "pause" && live_rml) {
            if (activate(window, "Quit")) {
                append_stage(19, play, "runtime quit");
            }
            return;
        }
        if (stage_ == 19 && !play.active()) {
            toolbar_click_attempted_ = false;
            capture("sdk-editor-recovered", play);
            // Editor is back to authored scene; the toolbar Play icon is
            // enabled+clipped again. Re-arm a real two-frame click to
            // start a fresh process and re-load from the project saves.
            ImVec2 center{};
            if (!toolbar_play_visible(center))
                return;
            arm_toolbar_click(center);
            append_stage(20, play, "toolbar play again");
            return;
        }
        if (stage_ == 20 && play.active() && page == "main" && live_rml) {
            // Restarted process must have a different session from the
            // saved one and publish the live main menu document before
            // we drive Continue.
            if (play.session().empty() || play.session() == save_session_)
                return;
            post_restart_session_ = play.session();
            capture("sdk-restart-loaded", play);
            if (activate(window, "Continue"))
                append_stage(21, play, "continue after restart");
            return;
        }
        if (stage_ == 21 && page == "play" && live_rml &&
            scene_asset(play) == reference::level_scene) {
            // Restarted Continue must also restore interactions + z.
            if (model_interactions(model) != 1)
                return;
            const auto z = player_position(play)[2];
            if (std::abs(z - save_z_) > 0.15)
                return;
            // Prove the Jump binding is still wired: wait for stable
            // Y on the restarted process, then a real rise from
            // baseline. We do not use the proven post-restart Continue
            // success to claim binding persistence on its own.
            if (!stable_y_settled(pre_restart_continue_jump_, player_position(play)[1],
                                  play.snapshot_version(), play.timing()))
                return;
            jump_baseline_y_ = player_position(play)[1];
            jump_baseline_recorded_ = false;
            tap_key(window, SDL_SCANCODE_J, SDLK_J);
            append_stage(22, play, "restarted persisted binding jump");
            return;
        }
        if (stage_ == 22) {
            const auto y = player_position(play)[1];
            if (!jump_baseline_recorded_) {
                if (y > jump_baseline_y_ + 0.15) {
                    jump_baseline_recorded_ = true;
                    peak_y_ = y;
                }
                return;
            }
            const auto version = play.snapshot_version();
            if (version != jump_last_version_) {
                if (y > peak_y_)
                    peak_y_ = y;
                if (y > jump_baseline_y_ + 0.05) {
                    jump_last_version_ = version;
                    return;
                }
                if (std::abs(y - jump_baseline_y_) > 1e-2) {
                    jump_last_version_ = version;
                    return;
                }
                capture("sdk-restarted-persisted-binding", play);
                tap_key(window, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
                append_stage(23, play, "paused for final quit");
                return;
            }
            return;
        }
        if (stage_ == 23 && page == "pause" && live_rml) {
            if (activate(window, "Quit"))
                append_stage(24, play, "final runtime quit");
            return;
        }
        if (stage_ == 24 && !play.active()) {
            capture("sdk-editor-usable", play);
            trace_["complete"] = true;
            trace_["scene_round_trip"] = true;
            trace_["save_load"] = true;
            trace_["binding_persisted_same_process"] = true;
            trace_["binding_persisted_restart"] = true;
            // Focus-gated menu routing coverage is mandatory; the
            // helper refuses to declare done() if the regression
            // never reached the gameplay-restore substate. The
            // captures + this flag are only written after actual
            // behavioural assertions (focus unchanged while
            // surrendered, focus changed while re-engaged) and a
            // real Resume navigation.
            trace_["focus_gated_routing"] = (outside_phase_ >= 8);
            done_ = true;
            return;
        }
    }

  private:
    static constexpr Uint64 kOverallBudgetMs = 240000; // SDK windows + WARP
    static constexpr Uint64 kStageBudgetMs = 20000;
    static constexpr Uint64 kStableWindowMs = 500;
};

} // namespace forge::test
