// Regression for the Windows79 SDK fixture init-order crash.
//
// The SDK workflow's `live_visible_context()` helper calls
// `Rml::GetNumContexts()` and `Rml::GetContext(int)` to enumerate
// RmlUi contexts. Both functions dereference the global `core_data`
// pointer (`ControlledLifetimeResource<CoreData>`, default-null)
// which is only populated inside `Rml::Initialise()`. The fixture's
// first main-loop iteration entered `frame_impl()` BEFORE
// `RuntimeUiHost::ensure_presenter()` -> `UiPresenter::Impl::Impl`
// -> `Rml::Initialise()` had run, so the probe dereferenced null
// `core_data` on the very first frame.
//
// The fix (tests/editor_sdk_workflow.hpp::find_live_visible_context)
// gates the probe on a caller-supplied `alive_probe` lambda. When
// the probe is absent or returns false the helper returns `nullptr`
// without touching RmlUi. main.cpp wires `runtime_ui.presenter_alive()`
// into the workflow via `set_presenter_alive_observer` AFTER the
// host is constructed (see `src/editor/main.cpp`), so the probe is
// safe to read on the first frame.
//
// This test exercises only the public static helper with RmlUi
// NOT initialised. A regression that re-introduces the un-guarded
// `Rml::GetNumContexts()` call would null-deref in this binary and
// abort instead of silently passing.
//
// The Windows80 retry regression: the previous incarnation of
// `ui_model(PlaySession&)` filtered `play.ui_snapshot().documents`
// by the hard-coded `reference::ui_id` entity. The authored
// reference project uses `reference::ui_id` for the level scene's
// Game UI entity (correct) but a per-regeneration UUID for the
// menu scene's Main menu UI entity (also correct — that entity id
// belongs to the authored scene, not to the fixture). The hard-
// coded filter therefore read the menu scene's page="" and
// timed out at stage 1. The fix selects the active document by
// visibility + single-match against the actual snapshot, so both
// scenes are read transparently.
//
// These tests drive the same code path through the
// `ui_model_from_snapshot` test seam so they execute without a
// full PlaySession construction.
#ifndef FORGE_UI_FIXTURE
#define FORGE_UI_FIXTURE 1
#endif
#include "editor_sdk_workflow.hpp"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>

namespace {
void require(bool value, const char* why) {
    if (!value) {
        std::cerr << "editor_sdk_workflow_probe_tests: FAIL " << why << std::endl;
        std::exit(2);
    }
}
// Menu-scene snapshot as the runtime produced it: a single visible
// document whose entity id is an arbitrary UUID (the authored menu
// project uses EntityId::generate() for the Main menu UI entity)
// and whose model is the page="main" publish from gameplay.cpp.
// A previous fixture hard-coded reference::ui_id and would have
// returned an empty model here.
const nlohmann::json menu_snapshot() {
    return {{"version", 2},
            {"session", "fa83bf14-4c7a-47b4-bf19-31694d46ee99"},
            {"generation", 1},
            {"revision", 58},
            {"errors", nlohmann::json::array()},
            {"documents",
             nlohmann::json::array({{{"entity", "a0b498f9-60c9-4b94-95da-bfb44536d908"},
                                     {"asset", "b52752da-8322-42b5-af86-c514b96662df"},
                                     {"instance", "a0b498f9-60c9-4b94-95da-bfb44536d908:7"},
                                     {"visible", true},
                                     {"layer", 0},
                                     {"model",
                                      {{"tick", 58},
                                       {"paused", true},
                                       {"page", "main"},
                                       {"message", ""},
                                       {"prompt", ""},
                                       {"interactions", 0},
                                       {"have_save", false},
                                       {"binding", "Choose Rebind Jump"},
                                       {"conflicts", ""},
                                       {"settings_text", ""}}},
                                     {"commands", {"Pause", "Resume", "Step"}}}})}};
}
// Level-scene snapshot: the Game UI entity carries the canonical
// reference::ui_id. The published model includes the same fields
// but with page="play" / pause interaction state. Used to prove
// the field-state preservation contract still holds when the
// authored entity id IS reference::ui_id.
const nlohmann::json level_snapshot() {
    return {{"version", 2},
            {"session", "fa83bf14-4c7a-47b4-bf19-31694d46ee99"},
            {"generation", 1},
            {"revision", 120},
            {"errors", nlohmann::json::array()},
            {"documents",
             nlohmann::json::array({{{"entity", "663e9451-b573-4dc7-811b-c9852c5e1203"},
                                     {"asset", "b52752da-8322-42b5-af86-c514b96662df"},
                                     {"instance", "663e9451-b573-4dc7-811b-c9852c5e1203:11"},
                                     {"visible", true},
                                     {"layer", 0},
                                     {"model",
                                      {{"tick", 120},
                                       {"paused", true},
                                       {"page", "play"},
                                       {"message", ""},
                                       {"prompt", "Interact to activate the beacon"},
                                       {"interactions", 1},
                                       {"have_save", false},
                                       {"binding", "key.e"}}},
                                     {"commands", {"Pause", "Resume", "Step"}}}})}};
}
} // namespace

int main() {
    // -------- find_live_visible_context safety --------
    // (1) Absent observer must yield nullptr without touching RmlUi.
    //     RmlUi Core is NOT initialised here; a regression that
    //     removed the gate would null-deref in GetNumContexts().
    {
        auto* ctx = forge::test::EditorSdkWorkflow::find_live_visible_context(nullptr);
        require(ctx == nullptr,
                "find_live_visible_context(nullptr) must return nullptr pre-Initialise");
    }
    // (2) Probe returning false must yield nullptr without touching RmlUi.
    {
        auto* ctx = forge::test::EditorSdkWorkflow::find_live_visible_context(
            std::function<bool()>([] { return false; }));
        require(ctx == nullptr,
                "find_live_visible_context(false-probe) must return nullptr pre-Initialise");
    }

    // -------- ui_model_from_snapshot document selection --------
    // (3) Menu scene: arbitrary entity id, single visible document,
    //     published page="main". A previous fixture that filtered
    //     by reference::ui_id would have returned an empty model
    //     here and stage 1 would have timed out.
    {
        const auto model = forge::test::EditorSdkWorkflow::ui_model_from_snapshot(menu_snapshot());
        require(!model.is_null() && model.is_object(),
                "menu snapshot: ui_model_from_snapshot must return object");
        require(model.value("page", "") == "main",
                "menu snapshot: model.page must surface the published payload");
        require(model.value("interactions", -1) == 0,
                "menu snapshot: model.interactions must surface the published payload");
        require(model.value("binding", "") == "Choose Rebind Jump",
                "menu snapshot: model.binding must surface the published payload");
    }
    // (4) Level scene: canonical reference::ui_id, single visible
    //     document, published page="play" + non-zero interactions
    //     + a real prompt. The fix must NOT regress this path —
    //     every existing stage after "New Game" reaches this
    //     shape and the field-state preservation contract is
    //     exactly what those stages read.
    {
        const auto model = forge::test::EditorSdkWorkflow::ui_model_from_snapshot(level_snapshot());
        require(!model.is_null() && model.is_object(),
                "level snapshot: ui_model_from_snapshot must return object");
        require(model.value("page", "") == "play", "level snapshot: model.page must be 'play'");
        require(model.value("interactions", -1) == 1,
                "level snapshot: model.interactions must be 1");
        require(model.value("prompt", "") == "Interact to activate the beacon",
                "level snapshot: model.prompt must surface the gameplay publish");
        require(model.value("binding", "") == "key.e",
                "level snapshot: model.binding must surface the gameplay publish");
    }
    // (5) Absent / unready: no documents at all (snapshot mid
    //     scene-swap or before the first publish) must yield an
    //     empty model, NOT a stale payload and NOT a crash.
    {
        const auto empty_snapshot = nlohmann::json{{"version", 2},
                                                   {"session", "x"},
                                                   {"generation", 0},
                                                   {"revision", 0},
                                                   {"errors", nlohmann::json::array()},
                                                   {"documents", nlohmann::json::array()}};
        const auto model = forge::test::EditorSdkWorkflow::ui_model_from_snapshot(empty_snapshot);
        require(model.is_object() && model.empty(),
                "empty snapshot: ui_model_from_snapshot must return empty object");
    }
    // (6) No `documents` key at all (defensive: a malformed
    //     response that drops the key entirely). Same answer:
    //     empty model.
    {
        const auto malformed =
            nlohmann::json{{"version", 2}, {"session", "x"}, {"generation", 0}, {"revision", 0}};
        const auto model = forge::test::EditorSdkWorkflow::ui_model_from_snapshot(malformed);
        require(model.is_object() && model.empty(),
                "malformed snapshot: ui_model_from_snapshot must return empty object");
    }
    // (7) Hidden / unready document: the only document is present
    //     but visible=false. This is the snapshot shape produced
    //     immediately after a scene replacement before the
    //     presenter has promoted the new document. The fixture
    //     must wait for visible=true, NOT silently fall back to
    //     the hidden document.
    {
        nlohmann::json snap = menu_snapshot();
        snap["documents"][0]["visible"] = false;
        snap["documents"][0]["model"]["page"] = "main-but-hidden";
        const auto model = forge::test::EditorSdkWorkflow::ui_model_from_snapshot(snap);
        require(
            model.is_object() && model.empty(),
            "hidden-only document: ui_model_from_snapshot must NOT fall back to the hidden model");
    }
    // (8) Unrelated document present alongside the real one:
    //     the editor's own (non-reference) UI document is
    //     visible at the same time as the gameplay document.
    //     The previous hard-coded reference::ui_id filter would
    //     have matched either one depending on which carried the
    //     matching entity id; the fixed policy is "single visible
    //     document", which means a scene that legitimately
    //     carries a second visible reference document must
    //     surface that as ambiguity rather than be silently
    //     narrowed to the wrong one.
    {
        nlohmann::json snap = menu_snapshot();
        // Pretend the editor is presenting its own UI document
        // alongside the reference one. The fixture must NOT
        // first-match the unrelated document.
        snap["documents"].insert(snap["documents"].begin(),
                                 {{"entity", "00000000-0000-0000-0000-000000000000"},
                                  {"asset", "99999999-9999-9999-9999-999999999999"},
                                  {"instance", "0:0"},
                                  {"visible", true},
                                  {"layer", 1},
                                  {"model", {{"page", "editor-toolbar"}}},
                                  {"commands", nlohmann::json::array()}});
        const auto model = forge::test::EditorSdkWorkflow::ui_model_from_snapshot(snap);
        require(model.is_object() && model.empty(),
                "ambiguous documents: ui_model_from_snapshot must return empty object, "
                "not first-match an unrelated model");
    }
    // (9) Same scene, but the unrelated document is hidden: the
    //     reference document is the only visible one, so the
    //     fixture must read it correctly even with unrelated
    //     hidden documents in the same snapshot.
    {
        nlohmann::json snap = menu_snapshot();
        snap["documents"].insert(snap["documents"].begin(),
                                 {{"entity", "00000000-0000-0000-0000-000000000000"},
                                  {"asset", "99999999-9999-9999-9999-999999999999"},
                                  {"instance", "0:0"},
                                  {"visible", false},
                                  {"layer", 1},
                                  {"model", {{"page", "editor-toolbar"}}},
                                  {"commands", nlohmann::json::array()}});
        const auto model = forge::test::EditorSdkWorkflow::ui_model_from_snapshot(snap);
        require(model.value("page", "") == "main",
                "single visible + hidden unrelated: must read the visible reference document");
    }
    std::cout << "editor_sdk_workflow_probe_tests: OK "
              << "(2 safe-probe paths + 7 ui_model_from_snapshot "
                 "document-selection cases before Rml::Initialise)"
              << std::endl;
    return 0;
}
