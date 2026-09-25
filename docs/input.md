# Fixed-tick gameplay input

`input.hpp/input.cpp` have no SDL dependency. Project input schema1 contains UUIDv4 ActionIds, display names, kind digital/axis1/axis2 and up to16 bindings per action (64 actions maximum). Binding IDs are FORGE control strings, not SDL enums. Supported keyboard keys, mouse buttons/delta/wheel and standard gamepad buttons/axes are enumerated by input_controls(). Unknown action/document fields survive settings round trips.

```text
SDL events → editor capture policy → ordered neutral InputEvents
    → protocol2 batches → RuntimeInput action mapping
    → const fixed-tick InputSnapshot → Flecs InputMonitor / future gameplay consumers
```

## Timing

RuntimeInput validates each batch completely before changing state. Continuous physical values are mapped to project actions. Each actual RuntimeSimulation tick latches one snapshot before the fixed pipeline; the same snapshot remains immutable through InputMonitor → NativeGameplay → FinalTransforms. InputMonitor counts digital edges without assigning gameplay meaning to action labels. ABI1 native modules do not gain action access in this phase; the monitor is the first runtime consumer and a no-code acceptance tool.

Digital held persists; pressed/released flags accumulate until the next actual tick and are cleared after consumption. Quick taps retain both flags; multiple same-type transitions within a tick interval coalesce. Catch-up ticks see held with no repeated edge. Relative mouse/wheel contributions are consumed once; continuous axes persist. Analog1D is clamped to[-1,1]; analog2D continuous length is clamped to1. Relative contributions are added afterward. Deadzones rescale gamepad axes. No caller/render delta determines input tick duration.

Pause does not latch input. Step consumes the pending snapshot once; Resume consumes it at the first fixed tick. Focus/capture/disconnect reset discards pending presses/deltas and neutralizes held controls; releases can be delivered once. This is not historical time-stamped reconstruction: inputs received between ticks are assigned to the next runtime latch. Future replay can record the explicit tick+ActionId snapshots; determinism/network transport are not claimed.

## Process and capture boundaries

Hello may carry simulation_hz and input_map; explicit runtime CLI frequency takes precedence. Existing protocol2 callers omitting them retain60Hz/empty-map behavior. Snapshot/Pause/Resume/Step requests may carry at most4096 ordered input_events. Existing session/request IDs reject stale envelopes. PlaySession retains a bounded queue; overflow replaces it with a neutral reset and diagnostic. Scene snapshots/persistence never contain transient input. Restart/recovery starts neutral; live module replacement clears input after successful load.

Capture is explicit in the Game panel during Play. Keyboard/mouse events go exclusively to the captured game except Esc/F6/F7. Release before editor text input; text/popup/file-operation/focus gates release capture. Without capture all editor controls retain their existing routes. The adapter owns one SDL gamepad connection; device removal releases state and closes its handle. Multiple players/devices, rumble, sensors and an elaborate Input Map UI are deferred; relative-mouse capture is supported through `GameInput::capture_checked(relative_mode=true)` so the cursor is hidden and motion continues without hitting the Game image edge.

The SDK Play path owns Escape explicitly. SDK Escape ships the neutral edge and the observed epoch through `GameInput::submit_external_release_observation(play, keep_routing=true)` BEFORE the gameplay down/up pair, so the runtime's release-guard sees the actual physical state and `RuntimeUiInput` still receives native menu keys. Legacy Escape only releases routing. Whether the game opens Pause or its in-game menu on Escape is project policy.

The SDK platform-effects adapter's eligibility test for the initial `cursor=false` → `GameInput::capture_checked(relative_mode=false)` menu-routing acquisition is `game_panel_visible && game_focused_ && play.ready() && !GameInput::captured()`. `game_panel_visible` reflects the existing `main.cpp` predicate `game_visible && input_allowed()` (visible Game, SDL window input focus, no text input, no popup/file busy). `game_focused_` is set by the host each frame from `ImGui::IsWindowFocused(...)` sampled immediately after the Game window's `Begin()` returns visible, and cleared by the adapter on focus loss, panel close, or `clear()` (Stop / project switch / restart). The pump runs AFTER the Game panel's `Begin()` so the adapter sees the CURRENT frame's focus and visibility flags, not the previous frame's stale values — the previous-frame bool issue is fixed in this batch. A hidden / unfocused capture offer is rejected with `play.platform_effects.cursor.hidden` or `play.platform_effects.cursor.unfocused` respectively. The Game window's "focused" check uses the existing `ImGuiFocusedFlags_ChildWindows`; the SDL relative-mode state is the single shared source of truth observed by `GameInput::captured()` / `relative()` via the existing `SdlGameCursor` (no parallel OS probe). Outside-panel surrender (a visible Game alongside a focused Scene / Console / Hierarchy panel) keeps logical routing off until the user re-focuses the Game panel; OS focus return alone does not restore routing — the user must click into Game.

The normal pointer UI surrender flow is Escape to pause, then click an editor control that does NOT replace the Game panel (e.g., the Hierarchy panel's "Expand all" button — Scene / Game are sibling tabs in the default dock, so clicking tab:Scene hides Game and breaks the regression; the Hierarchy control stays visible alongside Game). After the click is processed and a fresh snapshot is published, a Tab press through real SDL MUST NOT navigate the live Rml focus tree — runtime menu keys must not reach the menu while logical routing is off. Returning to gameplay requires clicking the live pause menu's **Capture gameplay input** button — clicking the Game tab does not re-engage relative mouse mode or enable menu navigation; only the Capture button does. After the click is processed, a Tab press MUST change the Rml focus element — runtime menu keys now reach the menu. The pause menu's "Resume" button is then activated via the existing `activate()` helper so a focused Resume selection exists, and Enter resumes gameplay with relative mouse mode on.

Official SDL headers at fa2c02bb6e21974a89ea9824bc53c9932abe5f9c were inspected for event fields, gamepad lifetime/ranges, keyboard scancodes and virtual-joystick tests. See [SDL_GetGamepads](https://wiki.libsdl.org/SDL3/SDL_GetGamepads), [SDL_OpenGamepad](https://wiki.libsdl.org/SDL3/SDL_OpenGamepad), and [SDL_AttachVirtualJoystick](https://wiki.libsdl.org/SDL3/SDL_AttachVirtualJoystick). ImGui capture helpers were checked at b48d1afbe8ee8b238e2961dc363a949dd7304e23. No dependency changed.

## Internal SDK consumer

Phase6A's separate exact native SDK exposes borrowed action snapshots through ActionId, alongside direct Flecs fixed-system registration. The existing ABI1 remains unchanged. See [Engine modules](engine-modules.md); the test module is transient and does not introduce engine-defined gameplay actions.

## Runtime UI routing

During captured Play input, Escape/F6/F7 retain editor control priority. RmlUi receives viewport mouse, keyboard, text and IME events next; gameplay receives unconsumed input. UI-owned releases remain consumed after focus changes. Capture changes and document replacement neutralize held gameplay input. The new SDL router is tested independently of Diligent against the real runtime. The menu-routing acquisition that lets RmlUi receive native menu keys is gated on the Game panel actually holding ImGui focus (see the eligibility rule above); visible-but-not-focused does not grant it. See [Runtime UI ownership and density](runtime-ui.md).

## Phase8 context and binding primitives (source checkpoint)

Input map version2 adds 1–32 named `contexts`. Each declares integer `priority`
(-10000…10000), `consume` (default true), and initial `active` (default false).
Each action belongs to exactly one declared context. Version1 maps retain their
existing always-active behavior. ActionIds remain the durable binding identity.

`RuntimeInput::activate_contexts` validates a complete replacement set before
changing it. Higher priority routes first; equal priority follows declaration
order, independent of activation-call order. A consuming context reserves its
bound controls from lower contexts, including when a control is up. Actions in
the same context may share a control; pass-through contexts do not reserve it.
Changing the active set clears pending presses and relative motion and releases
held actions. Reapplying the same set does not interrupt input.

`InputMap::with_bindings` creates a validated candidate without mutating the old
map. Empty bindings clear an action. Duplicate controls within a replacement
are rejected. `binding_conflicts` returns action/context ownership so a UI can
separate same-context conflicts from cross-context shadowing. Cross-context
sharing is not inherently an error: gameplay and menu actions commonly share keys.

`begin_rebind` listens through the existing RuntimeInput event path. It ignores
controls already held until released, stick noise below0.5 and incidental mouse
motion. A key, button, wheel or significant analog sample completes capture;
Escape or reset cancels. The captured sample retains direction. Captured batches
never also enter gameplay. The caller still validates and commits the candidate.

Stick-axis bindings may opt into `radial: true`. Pair both axes of the same stick
in the action with the same deadzone. Magnitude inside the deadzone maps to zero;
the remaining magnitude maps continuously to the unit disk. Higher-context
consumption of the other axis is respected. Existing axial deadzones remain the
default. SDL revision and runtime fixed-clock ownership are unchanged.

These are engine primitives under integration, not a claim that the standalone
Controls menu or gamepad/cursor completion has been delivered yet.

### Control frames and paused menus

Version2 contexts may set `phase: "control"` (default `"fixed"`). Control actions
are consumed by `latch_controls` and excluded from fixed snapshots. Separate edge
and relative-delta consumption cursors prevent a menu frame from consuming the
next gameplay tick's input, including deliberate pass-through mappings.

`GameSession::control_frame` invokes started module control callbacks only in the
active world. It also runs while paused, without advancing simulation. Reentrant
session mutation is rejected. A callback exception faults the world; it cannot be
resumed. Exact-SDK `controls` and `read_control` expose this boundary; the returned
sequence is a control-frame index, not a physics tick. UI publication/polling is
allowed in fixed ticks or control callbacks. Physics commands remain fixed-only.
These extensions are exact-build SDK changes; legacy gameplay ABI1 is unchanged.

### Runtime control callbacks and host requests

Exact-SDK modules can implement `controls` for menu actions while simulation is
paused. `read_control` reads only control-phase contexts; `read_action` remains
fixed-tick input. Neither callback may replace its own world. `FORGE_SDK_GAME`
provides copied queries and bounded request tokens; the host drains requests after
callbacks return. Tokens belong to the submitting module and world. Retirement
revokes pending requests and save schemas, retaining native code during any
synchronous validator/migration already executing.

`GameHostControls` delegates requests to `GameSession`, `GameStorage`, input and
narrow platform adapters. A scene candidate runs optional `scene_ready`, then
explicit saved-state restoration, before physics/resource admission. These stages
receive no gameplay input and cannot queue active-session mutations. A failed
candidate leaves the active scene intact.

Runtime UI actions remain argument-free unless the module explicitly registers a
value action. Value actions accept one bounded string, with copied two-pass SDK
polling. Semantic menu navigation uses RmlUi's own Tab/ShiftTab/Return behavior;
controller mapping remains in the normal input map.

A radial binding reads both axes of its stick even when it contributes only one
axis to an action. Both axes participate in context routing/conflict discovery;
a higher context can suppress either sample. Changes to the perpendicular axis
also update digital threshold edges. This keeps a one-axis radial binding radial,
instead of silently reverting to an axis-wise deadzone.
