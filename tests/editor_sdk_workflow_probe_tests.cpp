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
} // namespace

int main() {
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
    std::cout << "editor_sdk_workflow_probe_tests: OK (2 safe-probe paths before "
                 "Rml::Initialise)"
              << std::endl;
    return 0;
}
