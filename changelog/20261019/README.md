# 2026-10-19

## Standalone presentation display — source validated, delivery pending

- Optional `--show-fps` launch flag shows FPS, average frame time and the slowest frame in a small reporting window.
- The existing game UI renders the panel, including scenes without an authored HUD; it takes no gameplay input.
- Statistics use completed presentations and reset after minimizing. Simulation, VSync and device selection retain their existing settings.

Deterministic counter/launch checks pass normally and with ASan/UBSan and leak
inspection. The native RmlUi consumer covers literal text, no-canvas rendering,
input/focus, bounded toggles, candidate replacement and resource retirement.
Actual Vulkan pixels pass at seven sizes/densities, including changed text and exact
disabled-frame equivalence; representative captures were visually reviewed, including a tiny window at400% density.
The [Windows shared-game audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39186082370)
passed for source `cf5c8b1226b72549849c4f45d2dd792c10270342`: eleven focused
regressions, three reference checks, actual FPS/resize/fullscreen/input/minimize
captures, production startup with FPS enabled, source-free export and a fresh-runner
relaunch. Saved data survives a second move. Five native Windows captures were
reviewed. Both Windows/Linux core150/150 and Native SDK183/183 profiles passed at
the unchanged implementation source3501; cf5 only corrects the export test's
first-launch observation order, preserving its storage assertions.

This is not a new delivered Windows build. Physical performance testing remains
deferred. Software rendering verifies behavior; these counters do not establish
GPU duration or smooth frame pacing. The frame-export bundle's independent editor
gate still governs integration with main.
