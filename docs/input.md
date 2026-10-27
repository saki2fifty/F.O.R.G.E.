# Fixed-tick gameplay input

`input.hpp/input.cpp` have no SDL dependency. Project input schemas1/2 contain UUIDv4 ActionIds, display names, kind digital/axis1/axis2 and up to16 bindings per action (64 actions maximum); schema2 adds contexts. Binding IDs are FORGE control strings, not SDL enums. Supported keyboard keys, mouse buttons/delta/wheel, bounded touch contacts and standard gamepad buttons/axes are enumerated by input_controls(). Unknown action/document fields survive settings round trips.

```text
SDL events → editor capture policy → ordered neutral InputEvents
    → protocol2 batches → RuntimeInput action mapping
    → const fixed-tick InputSnapshot → Flecs InputMonitor / future gameplay consumers
```

## Timing

RuntimeInput validates each batch completely before changing state. Continuous physical values are mapped to project actions. Each actual RuntimeSimulation tick latches one snapshot before the fixed pipeline; the same snapshot remains immutable through InputMonitor → NativeGameplay → FinalTransforms. InputMonitor counts digital edges without assigning gameplay meaning to action labels. The retired movement-only interface did not expose these actions; project C++ gameplay now reads input through the exact SDK.

Digital held persists; pressed/released flags accumulate until the next actual tick and are cleared after consumption. Quick taps retain both flags; multiple same-type transitions within a tick interval coalesce. Catch-up ticks see held with no repeated edge. Relative mouse/wheel contributions are consumed once; continuous axes persist. Analog1D is clamped to[-1,1]; analog2D continuous length is clamped to1. Relative contributions are added afterward. Deadzones rescale gamepad axes. No caller/render delta determines input tick duration.

Pause does not latch input. Step consumes the pending snapshot once; Resume consumes it at the first fixed tick. Focus/capture/disconnect reset discards pending presses/deltas and neutralizes held controls; releases can be delivered once. This is not historical time-stamped reconstruction: inputs received between ticks are assigned to the next runtime latch. Future replay can record the explicit tick+ActionId snapshots; determinism/network transport are not claimed.

## Process and capture boundaries

Hello may carry simulation_hz and input_map; explicit runtime CLI frequency takes precedence. Existing protocol2 callers omitting them retain60Hz/empty-map behavior. Snapshot/Pause/Resume/Step requests may carry at most4096 ordered input_events. Existing session/request IDs reject stale envelopes. PlaySession retains a bounded queue; overflow replaces it with a neutral reset and diagnostic. Scene snapshots/persistence never contain transient input. Restart/recovery starts neutral; live module replacement clears input after successful load.

Capture is explicit in the Game panel during Play. Keyboard/mouse events go exclusively to the captured game except Esc/F6/F7. Release before editor text input; text/popup/file-operation/focus gates release capture. Without capture all editor controls retain their existing routes. SdlGamepads owns up to16 connected SDL handles and four independent logical slots, plus the legacy active-controller alias. Disconnect neutralizes only the affected logical slot and active alias, then closes its handle. Sensors remain deferred; relative-mouse capture is supported through `GameInput::capture_checked(relative_mode=true)` so the cursor is hidden and motion continues without hitting the Game image edge.

## Local controller slots

`pad.1.*` through `pad.4.*` expose the same 15 buttons and six axes as legacy
`pad.*`, with the same ranges. Sticks pair only within their logical slot for
radial conditioning and context consumption. Slot controls are appended to the
existing discovery registry, preserving earlier IDs/order. `pad.0.*` and
`pad.5.*` are invalid. Input/settings schemas and persistence owners are unchanged.
The legacy alias still selects deliberate input after its 250ms guard; this guard
does not block another slot's input. Binding both namespaces deliberately reads
the same physical device twice. UI navigation and the global device prompt retain
their existing active-controller policy; no per-player UI-focus system is implied.
Revision protects connection/assignment topology; the active alias may change
without advancing it. Read the current observation when that alias matters.

New connections fill the lowest free slot. Further tracked devices are unassigned
but can supply the legacy alias. The platform owner issues opaque connection
tokens and a monotonically increasing topology revision. Tokens are scoped to
one open connection and host, never written to projects/user bindings. Disconnect,
remap and assignment advance the revision. Reassignment can unassign a device
with slot 0 or displace an occupied slot. It prepares zero samples for only the
old/new logical namespaces and stops their owned feedback before changing routing.
A failed native Stop leaves assignments/revision unchanged and retryable.
Keyboard/touch, the legacy alias and other slots are preserved. Global focus,
capture/modal/Stop resets remain global under their existing owners. Newly
assigned devices need fresh input; held native controls are not replayed.

Game panel **Gamepads...** uses GameInput's same assignment operation. Its popup
obeys the ordinary UI capture-release policy. Automation/MCP query and assignment
delegate to that owner too. They preflight space for at most 42 neutral input
events so a physical edit cannot itself overflow Play's 4096-event queue and
trigger the global emergency reset. This is transient host routing, with no
authored Undo or saved player identity.

The copied `GamepadTopology` record is `{available,revision,active,devices}`.
Each device has `{device,name,slot,connected,rumble}`. `available:false` means the
native adapter is unavailable, distinct from an available empty list. Names are
bounded sanitized display text; they are not identity keys. `GamepadAssignment`
requires `{revision,device,slot}` with current positive revision and slot 0..4.
Records validate completely before replacing their destination. SDK Play sends
changed topology on the existing correlated `snapshot` request's `gamepads` field;
metadata cannot run on another command or roll its revision backwards. Device
observations do not advance the cursor epoch or cancel unrelated pending effects.

The SDK Play path owns Escape explicitly. SDK Escape ships the neutral edge and the observed epoch through `GameInput::submit_external_release_observation(play, keep_routing=true)` BEFORE the gameplay down/up pair, so the runtime's release-guard sees the actual physical state and `RuntimeUiInput` still receives native menu keys. Legacy Escape only releases routing. Whether the game opens Pause or its in-game menu on Escape is project policy.

The SDK platform-effects adapter's eligibility test for the initial `cursor=false` → `GameInput::capture_checked(relative_mode=false)` menu-routing acquisition is `game_panel_visible && game_focused_ && play.ready() && !GameInput::captured()`. `game_panel_visible` reflects the existing `main.cpp` predicate `game_visible && input_allowed()` (visible Game, SDL window input focus, no text input, no popup/file busy). `game_focused_` is set by the host each frame from `ImGui::IsWindowFocused(...)` sampled immediately after the Game window's `Begin()` returns visible, and cleared by the adapter on focus loss, panel close, or `clear()` (Stop / project switch / restart). The pump runs AFTER the Game panel's `Begin()` so the adapter sees the CURRENT frame's focus and visibility flags, not the previous frame's stale values — the previous-frame bool issue is fixed in this batch. A hidden / unfocused capture offer is rejected with `play.platform_effects.cursor.hidden` or `play.platform_effects.cursor.unfocused` respectively. The Game window's "focused" check uses the existing `ImGuiFocusedFlags_ChildWindows`; the SDL relative-mode state is the single shared source of truth observed by `GameInput::captured()` / `relative()` via the existing `SdlGameCursor` (no parallel OS probe). Outside-panel surrender (a visible Game alongside a focused Scene / Console / Hierarchy panel) keeps logical routing off until the user re-focuses the Game panel; OS focus return alone does not restore routing — the user must click into Game.

The normal pointer UI surrender flow is Escape to pause, then click an editor control that does NOT replace the Game panel (e.g., the Hierarchy panel's "Expand all" button — Scene / Game are sibling tabs in the default dock, so clicking tab:Scene hides Game and breaks the regression; the Hierarchy control stays visible alongside Game). After the click is processed and a fresh snapshot is published, a Tab press through real SDL MUST NOT navigate the live Rml focus tree — runtime menu keys must not reach the menu while logical routing is off. Returning to gameplay requires clicking the live pause menu's **Capture gameplay input** button — clicking the Game tab does not re-engage relative mouse mode or enable menu navigation; only the Capture button does. After the click is processed, a Tab press MUST change the Rml focus element — runtime menu keys now reach the menu. The pause menu's "Resume" button is then activated via the existing `activate()` helper so a focused Resume selection exists, and Enter resumes gameplay with relative mouse mode on.

Official SDL headers at fa2c02bb6e21974a89ea9824bc53c9932abe5f9c were inspected for event fields, gamepad lifetime/ranges, keyboard scancodes and virtual-joystick tests. See [SDL_GetGamepads](https://wiki.libsdl.org/SDL3/SDL_GetGamepads), [SDL_OpenGamepad](https://wiki.libsdl.org/SDL3/SDL_OpenGamepad), and [SDL_AttachVirtualJoystick](https://wiki.libsdl.org/SDL3/SDL_AttachVirtualJoystick). ImGui capture helpers were checked at b48d1afbe8ee8b238e2961dc363a949dd7304e23. No dependency changed.

## Internal SDK consumer

Phase6A's separate exact native SDK exposes borrowed action snapshots through ActionId, alongside direct Flecs fixed-system registration. The retired movement-only ABI1 interface is no longer a project gameplay path. See [Engine modules](engine-modules.md); the test module is transient and does not introduce engine-defined gameplay actions.

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

Editor Play publishes the runtime-owned `rebind_listening` flag with its input
snapshot. While true, keyboard and mouse binding events go directly to the
runtime listener before RmlUi can consume them; their releases follow the same
route. UI-generated neutral input is delivered before a UI command that starts
rebinding, so it cannot cancel the newly started listener. Escape, loss of focus
and explicit reset still cancel the listener.

`begin_rebind(gamepad_slot=0)` listens through the existing RuntimeInput event path. It ignores
controls already held until released, stick noise below0.5 and incidental mouse
motion. A key, button, wheel or significant analog sample completes capture;
Escape or reset cancels. The captured sample retains direction. Captured batches
never also enter gameplay. The caller still validates and commits the candidate.
The slot filter accepts only its gamepad namespace, avoiding duplicate legacy/
indexed aliases and other players. Keyboard/mouse/touch remain eligible. The Game
service infers it from an existing binding, or accepts explicit `gamepad_slot`0..4
on `rebind_begin`; copied `rebind.gamepad_slot` reports the selected filter.

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
These extensions are exact-build SDK changes; project C++ gameplay uses only the exact SDK.

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

## Gamepad feedback

The existing Game service accepts a scoped `rumble` operation with numeric `low`
and `high` strengths0..1 and integral `duration_ms`0..5000. Nonzero strength
requires positive duration. Optional `slot` 1..4 targets that logical controller;
omitted/0 targets the legacy active controller. Zero strengths with slot 0 stop
all owned feedback; a specific slot stops only its current device. The shared neutral value
validator is reused by host dispatch and editor wire admission. This is transient
real-time device output, unaffected by simulation speed, not stored action data.

SdlGamepads owns native feedback per connected handle. Pinned SDL's
`SDL_RumbleGamepad` replaces its previous pulse and supplies the duration stop;
FORGE quantizes normalized strengths to unsigned16-bit levels. Missing/unsupported
controllers and native driver failures reject honestly. A host adapter's presence
(`game_query.gamepad_rumble_host`) does not assert hardware capability.

SDK Play uses the existing correlated platform offer/ack lifecycle with additive
kind `rumble` and canonical `{low,high,duration_ms,slot?}` value. The editor applies a
fresh tuple once through GameInput, requires a visible focused Game panel with
logical input routing, and acknowledges the actual native result. Zero-strength
Stop is permitted after routing/focus loss. Requests can run from control callbacks
while simulation is paused; Pause transitions stop preceding feedback.

Focus/routing loss, Stop, session/world/epoch changes, active-controller changes,
device removal and teardown request a native Stop. Active-alias changes stop its
previous legacy feedback, preserving independent indexed output. A failed native Stop is logged. Expiry also requires ongoing SDL event updates
and a working driver; the5000ms limit bounds requests, not a guarantee that failing
hardware stops. Releasing a completed receipt does not roll back a pulse.
Trigger motors, force feedback and mixing/layered haptics are not exposed.
No new C ABI callback, SDK layout or dependency pin.
The public `sdk/sample/gamepad_feedback_probe.cpp` demonstrates scoped requests,
receipt polling/release and nonfatal device failures.

## Analog conditioning

Optional per-binding `outer_deadzone` defaults0 and `response_exponent` defaults1
in input maps1/2. Append-only InputBinding fields preserve existing aggregate
positions. InputMap admits finite numeric values: inner/outer nonnegative with
positive remaining travel, exponent0.1..8. Nondefault conditioning on buttons,
keyboard or relative controls rejects; it is not silently ignored. Input schema,
ActionIds and the installed C++ ABI/fingerprint remain unchanged.

For absolute axis magnitude a, inner d and outer o, map
`u = clamp((a-d)/(1-d-o), 0, 1)`, then apply `u^exponent`, preserving the sign.
Radial sticks use circular length (capped at1 for saturation), retain X/Y direction,
and respect both routed samples. Digital press thresholds follow conditioning;
contribution scales and combined action clamps retain their existing order.
The default exponent avoids pow; the previous radial default formula is retained.

ProjectSettings validation composes Game defaults through the existing
resolve_game_settings owner. Its default/player inner-deadzone overrides cannot
silently produce an impossible range with the binding's outer value. Invalid
candidates reject before project or player settings publication; last-good input
and preferences remain. Rebinding/control changes clear the previous response
fields with the existing deadzone/radial reset policy. Unknown extensions remain
preserved by normal source round trips. Fixed and control latches use the same
RuntimeInput evaluator; no new clock, input manager or event reconstruction.

The editor presents Outer deadzone and Response exponent on continuous analog
bindings. MCP edits these fields through existing project.settings.edit/save and
observes actual values through play.input/input_submit; there are no parallel
agent-only input transformations. See[the input guide](../manual/editor/input.md).

## Window touch contacts

`input_controls` owns eight transient `touch.N.down/x/y/pressure` semantic groups. Down is digital; positions/pressure are unipolar0..1. `InputControl.unipolar` also models the existing trigger range. `validate_input_event` is the shared runtime/automation value admission owner, preserving atomic batch validation. The native Settings picker hides meaningless negative directions for unipolar controls; MCP discovery reports numeric ranges. InputMap schemas1/2, ActionIds, SDK C tables and fixed/control latch ownership are unchanged. Generic Axis2 normalizes continuous length: independent screen coordinates use two Axis1 actions. Rebind listens to touch-down, not coordinate/pressure motion.

`SdlTouches` is one bounded main-thread platform adapter shared by GameInput and game_main. It keys eight stable slots by transient(device,finger), assigns only fresh in-bounds DOWN, ignores overflow/unknown motion/synthetic mouse or pen touches, validates finite normalized samples, clamps tracked image-relative movement and clears all values on UP/CANCELED. Slots never compact; reuse requires a new DOWN. Window/viewport changes or ownership loss release contacts. No SDL ID is saved or exposed to project data. The borrowed SDL window outlives its adapter.

Editor touch follows current Game capture and image rectangle. GameInput observes PlaySession's local neutral-queue serial, so a UI/automation/overflow reset invalidates platform contacts before subsequent motion. This is a derived queue observation, not another authoritative input state. Focus/capture/Stop also reset contacts. SDL queues touch-generated mouse events before raw finger events. RuntimeUiInput's existing UI-button ownership blocks raw contacts during a claimed pointer gesture; unclaimed contacts reach GameInput, which suppresses synthetic mouse gameplay duplicates. Editor interaction outside capture retains SDL mouse emulation.

The standalone host admits focused, unclaimed raw touch against the full window, with no relative mouse requirement. Existing Rml pointer callbacks claim a touch gesture before its raw finger DOWN; UI claims clear gameplay contacts and neutralize input. While UI owns its pointer gesture, other fingers cannot join gameplay. Focus/cursor/session changes clear contacts. Mouse-generated touch and touch-generated gameplay mouse values are filtered. Sensors/pen data, multiple-player device assignment and mobile hosting are not implemented.

Original Gallery88 and `TouchInput.hpp/TouchCursor.cpp` use four saved ActionIds, native fixed queries and ordinary Sprite/Transform values. The installed sample builds the same example source rather than copying behavior. Source-free content retains the canvas/image identity; physical/native Windows acceptance is separate.

## Two-contact gesture deltas

SdlTouches derives one stable pair from its existing bounded contacts. It retains the pair while both slots are occupied, rebases replacements without an impulse, and reports `touch.gesture.active` while at least two admitted contacts exist. Other fingers and pressure do not change the pair. No separate recognizer database, clock, device identity, platform path or authored state is introduced.

`touch.gesture.pan_x/pan_y` are relative normalized centroid deltas, positive right/down. `pinch` is the difference of logarithmic pixel distances (sum deltas then exponentiate for scale); `rotation` is the shortest wrapped screen-clockwise angular difference in radians. Pixel-space vectors use the Game image's actual aspect. Subpixel/coincident pairs suspend scale/rotation and rebase after separation; finite guards avoid division/overflow. Relative controls reuse RuntimeInput validation/accumulation and independent fixed/control latches. The active digital state does not gate already emitted relative contributions: a gesture ending before a tick retains its movement, like mouse/wheel input. Explicit neutral input resets discard pending deltas under the existing owner; pair/layout rebasing alone does not rewrite previously admitted input.

Control discovery describes units/ranges; Settings binds through its existing collection/combo, with no deadzone/response fields for relative controls. MCP submits the same generation-checked semantic events. Ordinary Play can use Pause/Step while physical capture is released for exact one-latch observation. SDK Step uses its authoritative cursor-release path and neutralizes pending input; use running playback for SDK pulse consumption. Synthetic semantic contacts do not invoke SDL recognition; agents may inject the desired semantic gesture controls directly. GameInput's keep-routing release now neutralizes pending input just like its checked peer while retaining future menu routing, so old deltas cannot survive capture release.

Original Gallery89 and its installed public C++ sample pan/scale/rotate ordinary authored transforms in the runtime copy, apply deltas once without dt, clamp example pan/scale and restore source at restart. There is no engine camera/zoom policy, inertia or new persistence format. Native Windows/full Main/game_main and physical/graphical acceptance remain separately queued.

## Single-contact classifications

The same SdlTouches owner emits digital `touch.gesture.tap`, `double_tap`,
`long_press` and `swipe_left/right/up/down`. Every recognition is an immediate1/0
pulse. RuntimeInput retains its pressed/released edges once in each eligible fixed
or control latch; repeated identical pulses before one latch coalesce into one
edge pair, following existing digital semantics. This is not a held button or a separate action system. Settings,
rebinding, C++ and MCP use existing control discovery and digital validation.

A tap releases within250ms and never moves more than8 window-coordinate pixels
from its start. Two consecutive taps on the same touch device additionally emit
double_tap if their releases are within350ms and24pixels; both taps also emit Tap
immediately. The second clears the pair, so a third starts another pair. Long press
fires once at500ms while maximum excursion remains within8pixels; it suppresses
release Tap/Swipe. A swipe releases within750ms with at least64pixels net movement;
the dominant axis selects direction, ties horizontal, positiveY down. These fixed
first-profile thresholds use Game/window coordinates, not framebuffer resolution
or simulation-scaled time. Configurable thresholds, inertia and drag recognition
are unavailable.

Only one candidate and one previous tap are retained. A second admitted contact
cancels single-contact classification until all contacts lift and a fresh down
starts; existing two-contact deltas retain their semantics. Cancel, invalid candidate
samples, backward observed timestamps, focus/capture/neutral/reset and viewport or
window changes discard candidates and prior-tap history. Maximum excursion prevents
an out-and-back movement from becoming a tap. Final valid UP coordinates participate
in classification without changing the existing raw contact release values.

Pinned SDL event timestamps and SDL_GetTicksNS share the nanosecond clock. Both
hosts call the owner's hold advance after draining events, so an already queued
early UP is processed before a delayed frame can emit a hold. Native clock reads
are skipped without an eligible pending hold. GameInput observes the existing
neutral serial again at that boundary. No new clock owner, thread, device registry
or persistent format is introduced. Semantic MCP contacts do not run the SDL
classifier; agents submit intended recognition1/0 pulses directly.

Gallery91 and `CodeExamples/SingleTouch.cpp` use seven saved ActionIds, public SDK
pressed edges and ordinary runtime transforms. Tap turns90degrees, Double tap
restores transforms, Long press toggles scale1/.5 and swipes move one world unit
within the canvas bounds. The installed `touch_gestures_gameplay` sample wraps both
Gallery89 and91 systems, each scoped to its own Scene/entity. SDK values, C tables,
ABI and dependency pins remain unchanged. Portable timing/queued-event tests do
not establish physical touch latency, native Windows/Main or graphical export.
