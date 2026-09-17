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

Capture is explicit in Scene during Play. Keyboard/mouse events go exclusively to the captured game except Esc/F6/F7. Release before editor text input; text/popup/file-operation/focus gates release capture. Without capture all editor controls retain their existing routes. The adapter owns one SDL gamepad connection; device removal releases state and closes its handle. Multiple players/devices, rumble, sensors, gameplay cursor locking and an elaborate Input Map UI are deferred.

Official SDL headers at fa2c02bb6e21974a89ea9824bc53c9932abe5f9c were inspected for event fields, gamepad lifetime/ranges, keyboard scancodes and virtual-joystick tests. See [SDL_GetGamepads](https://wiki.libsdl.org/SDL3/SDL_GetGamepads), [SDL_OpenGamepad](https://wiki.libsdl.org/SDL3/SDL_OpenGamepad), and [SDL_AttachVirtualJoystick](https://wiki.libsdl.org/SDL3/SDL_AttachVirtualJoystick). ImGui capture helpers were checked at b48d1afbe8ee8b238e2961dc363a949dd7304e23. No dependency changed.

## Internal SDK consumer

Phase6A's separate exact native SDK exposes borrowed action snapshots through ActionId, alongside direct Flecs fixed-system registration. The existing ABI1 remains unchanged. See [Engine modules](engine-modules.md); the test module is transient and does not introduce engine-defined gameplay actions.
