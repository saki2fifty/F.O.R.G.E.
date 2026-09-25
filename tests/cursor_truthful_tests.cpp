// Durable test shim for the SdlGameCursor / GameInput cursor correction.
// Exercises the contract:
//   - captured() / relative() reflect the live OS probe; no cached
//     intent lies about physical state.
//   - capture() requires focus, validates the getter, and throws on
//     disagreement (setter-true / getter-false).
//   - checked_release() returns true only when the OS confirms
//     relative mode is off; no setter call when getter already off.
//   - release() (noexcept) routes failure to the diagnostic log path.
//
// Coverage of GameInput is limited to the sdl_key_control spelling; the
// GameInput routing / Escape contract depends on a live PlaySession
// runtime and is exercised by the editor-process tests, not here.
//
// The stub (tests/sdl_cursor_stub.cpp) provides the SDL_SetWindow* /
// SDL_GetWindow* / SDL_LogWarn symbols so the test process does not
// require a real SDL3 runtime. SDL_Init / SDL_Quit are NOT called: this
// test does not own the SDL runtime; main_game does.
#include "sdl_game_cursor.hpp"
#include "sdl_input.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <string>

extern std::atomic<int> g_set_calls;
extern std::atomic<bool> g_set_returns_true;
extern std::atomic<bool> g_set_records_intent;
extern std::atomic<bool> g_get_returns_true;
extern std::atomic<Uint64> g_window_flags;
extern std::atomic<Uint32> g_window_id;

static int failures = 0;
static int checks = 0;
static const char* g_current = "?";
#define REQUIRE(cond, msg)                                                                         \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            ++failures;                                                                            \
            std::fprintf(stderr, "[%s] FAIL %s\n", g_current, msg);                                \
        } else {                                                                                   \
            std::fprintf(stderr, "[%s] OK %s\n", g_current, msg);                                  \
        }                                                                                          \
    } while (0)

static void reset(bool focused = true, bool physically_on = false, bool setter_returns = true,
                  bool records_intent = true) {
    g_set_calls = 0;
    g_set_returns_true = setter_returns;
    g_set_records_intent = records_intent;
    g_get_returns_true = physically_on;
    g_window_flags = focused ? SDL_WINDOW_INPUT_FOCUS : 0;
    g_window_id = 1;
}

// ---- SdlGameCursor ----

// Setter reports success but the live getter stays off: capture() must
// throw, captured() / relative() must both report false.
static void test_case_capture_getter_disagreement() {
    g_current = "capture_setter_true_getter_false";
    reset(true, false, true, false);
    forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    bool threw = false;
    try {
        cursor.capture();
    } catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw, "capture() must throw when setter reports success but getter is off");
    REQUIRE(!cursor.relative(),
            "relative() must be false after failed capture (getter never flipped)");
    REQUIRE(!cursor.captured(), "captured() must be false after failed capture");
}

// Happy path: setter succeeds, getter agrees, captured() / relative()
// both return true.
static void test_case_capture_happy_path() {
    g_current = "capture_happy_path";
    reset(true, false, true, true);
    forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    bool threw = false;
    try {
        cursor.capture();
    } catch (...) {
        threw = true;
    }
    REQUIRE(!threw, "happy-path capture() should not throw");
    REQUIRE(cursor.relative(), "relative() must report true after setter+getter agree");
    REQUIRE(cursor.captured(), "captured() must report true after setter+getter agree");
}

// External mode flip: cached state cannot lie about physical state.
// With no captured_, checked_release just consults the live getter and
// performs the release.
static void test_case_checked_release_attempts_when_physical_on() {
    g_current = "checked_release_with_physical_on";
    reset(true, false, true, true);
    forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    // Simulate external mode change: OS now says relative is on.
    g_get_returns_true = true;
    REQUIRE(cursor.relative(), "physical relative must be true (external flip)");
    REQUIRE(cursor.captured(), "captured() must also reflect the live getter after external flip");
    // checked_release must succeed.
    const bool ok = cursor.checked_release();
    REQUIRE(ok, "checked_release must succeed when setter flips getter off");
    REQUIRE(g_set_calls.load() == 1, "checked_release must have called the setter");
    REQUIRE(!cursor.relative(), "physical relative must be off after release");
    REQUIRE(!cursor.captured(), "captured() must be false after release");
}

// Setter fails AND getter still on (the failure can coincide).
// checked_release must return false; captured() / relative() reflect
// the physical state (still on).
static void test_case_checked_release_setter_fails() {
    g_current = "checked_release_setter_fails";
    reset(true, true, true, false);
    forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    g_set_returns_true = false;
    g_get_returns_true = true;
    const bool ok = cursor.checked_release();
    REQUIRE(!ok, "checked_release must return false when setter fails");
    REQUIRE(cursor.relative(), "physical state must still report relative on (setter false)");
    REQUIRE(cursor.captured(), "captured() must reflect the live getter after failed release");
}

// Destructor with a failing setter must NOT throw and must NOT crash.
// The physical state stays on (getter still says relative).
static void test_case_destructor_noexcept_best_effort() {
    g_current = "destructor_noexcept_best_effort";
    reset(true, true, true, false);
    g_set_returns_true = false;
    {
        forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
        bool threw = false;
        try {
            cursor.capture();
        } catch (...) {
            threw = true;
        }
        REQUIRE(threw, "capture() must throw when setter fails");
    }
    REQUIRE(true, "destructor did not throw on failed setter");
}

static void test_case_capture_requires_focus() {
    g_current = "capture_requires_focus";
    reset(false, false, true, true);
    forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    bool threw = false;
    try {
        cursor.capture();
    } catch (...) {
        threw = true;
    }
    REQUIRE(threw, "capture() must throw when window has no input focus");
}

static void test_case_release_no_op_when_already_off() {
    g_current = "checked_release_no_op_when_already_off";
    reset(true, false, true, true);
    forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    const int before = g_set_calls.load();
    REQUIRE(cursor.checked_release(),
            "checked_release returns true when physical state is already off");
    REQUIRE(g_set_calls.load() == before, "checked_release must not call setter when already off");
}

// Destructor with physical on and setter succeeding must flip getter off.
static void test_case_destructor_with_physical_on() {
    g_current = "destructor_with_physical_on";
    reset(true, true, true, true);
    {
        forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    }
    REQUIRE(g_get_returns_true.load() == false,
            "destructor must turn the OS probe off when setter succeeds");
}

static void test_case_destructor_setter_fails() {
    g_current = "destructor_setter_fails";
    reset(true, true, true, false);
    g_set_returns_true = false;
    bool destroyed = false;
    {
        forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
        destroyed = true;
    }
    REQUIRE(destroyed, "destructor must not throw");
    REQUIRE(g_get_returns_true.load() == true,
            "physical mode still on after destructor with setter failure");
}

// Setter fails while physical mode is ON. checked_release must
// return false because the setter refused (and the getter agrees
// afterwards because the setter flipped it off, even though
// g_set_returns_true=false means the SDL call returned false).
// The adapter must NOT pretend success; captured() stays
// truthful: setter changes getter off but returns false.
static void test_case_checked_release_setter_fails_getter_off() {
    g_current = "checked_release_setter_fails_getter_off";
    // Cursor starts physically ON. Setter enabled (records intent
    // = flips getter to enabled value), but setter returns false.
    // The setter flips the getter off (enabled=false), so the
    // adapter sees a getter-off state after the call.
    reset(true, true, true, true);
    forge::SdlGameCursor cursor(reinterpret_cast<SDL_Window*>(0xC0DE));
    g_set_returns_true = false;
    const bool ok = cursor.checked_release();
    REQUIRE(!ok, "checked_release must return false when setter refuses");
    REQUIRE(!cursor.relative(), "relative() must report false after setter flips getter off");
    REQUIRE(!cursor.captured(), "captured() must report false after setter flips getter off");
}

// ---- sdl_key_control naming ----

static void test_case_escape_naming() {
    g_current = "escape_naming";
    const auto esc = forge::sdl_key_control(SDL_SCANCODE_ESCAPE);
    REQUIRE(esc == "key.escape", "Escape must produce the 'key.escape' control spelling");
    REQUIRE(!esc.empty(), "Escape must produce a non-empty control spelling");
}

static void test_case_other_keys() {
    g_current = "other_keys";
    REQUIRE(forge::sdl_key_control(SDL_SCANCODE_A) == "key.a", "key.a");
    REQUIRE(forge::sdl_key_control(SDL_SCANCODE_5) == "key.5", "key.5");
    REQUIRE(forge::sdl_key_control(SDL_SCANCODE_F6).empty(), "F6 has no control");
    REQUIRE(forge::sdl_key_control(SDL_SCANCODE_F7).empty(), "F7 has no control");
}

int main() {
    test_case_capture_happy_path();
    test_case_capture_getter_disagreement();
    test_case_checked_release_attempts_when_physical_on();
    test_case_checked_release_setter_fails();
    test_case_checked_release_setter_fails_getter_off();
    test_case_destructor_noexcept_best_effort();
    test_case_destructor_with_physical_on();
    test_case_destructor_setter_fails();
    test_case_capture_requires_focus();
    test_case_release_no_op_when_already_off();
    test_case_escape_naming();
    test_case_other_keys();
    std::fprintf(stderr, "checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}