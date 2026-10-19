# 2026-10-19

## Standalone presentation display — local implementation

- Optional `--show-fps` launch flag shows FPS, average frame time and the slowest frame in a small reporting window.
- The existing game UI renders the panel, including scenes without an authored HUD; it takes no gameplay input.
- Statistics use completed presentations and reset after minimizing. Simulation, VSync and device selection retain their existing settings.

Deterministic counter/launch checks pass normally and with ASan/UBSan and leak
inspection. The native RmlUi consumer covers literal text, no-canvas rendering,
input/focus, bounded toggles, candidate replacement and resource retirement.
Actual Vulkan pixels pass at seven sizes/densities, including changed text and exact
disabled-frame equivalence; representative captures were visually reviewed, including a tiny window at400% density.
The real Windows game, fullscreen/minimize workflow and source-free production
launch/relocation checks are implemented but have not executed for this source.
This is not a new delivered Windows build. Physical performance testing remains
deferred; these counters do not establish GPU duration or smooth frame pacing.
