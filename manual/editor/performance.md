# Performance and diagnostics

The permanent bottom bar helps you see how the editor is running. Console explains recent operations and failures.

## Read the counters

**FPS** and mean frame time describe the editor's frame loop. Lower frame time means more frames per second. These are not GPU execution-time measurements.

**CPU** is the editor process's usage normalized across logical processors. **RAM** is its resident memory in MiB. Separate gameplay/compiler processes are excluded. Counters update about every half second; unavailable values show `--`.

**VSync off** means the application requests unsynchronized presentation and imposes no FPS cap. Driver or compositor settings can still affect observed rates. The bar also shows Edit/Play state and authored entity count.

## See where frame time goes

Open **Tools → Performance**. Its CPU frame sections update about every half second:

- **Update and editor UI** covers input, workers, authoring data, panels and overlays.
- **Scene render submission** covers the CPU work to redraw or reuse the Scene image.
- **UI render submission** covers the backbuffer and Dear ImGui rendering commands.
- **Present call** measures time spent handing the frame to Diligent/Windows.

These sections measure CPU wall time. GPU work runs asynchronously; waits can occur during submission or presentation. A GPU bottleneck cannot be diagnosed from these numbers alone.

**Scene redraws / Retained** counts full scene renders and unchanged EDIT image reuses since launch. An idle static scene can reuse its image while the interface continues drawing uncapped. Moving the camera, editing a primitive, resizing or previewing a transform refreshes the image. Play always redraws. This optimization currently applies to static blockout geometry; idle editor FPS is not gameplay performance.

### Compare a slowdown

1. Keep the same window size, panel layout and camera view.
2. Compare an empty scene, one cube and two cubes. Keep selection consistent: selecting an object adds Inspector controls and an outline.
3. Wait a second after each change, then note FPS, frame time and the four CPU sections.
4. Compare idle viewing with orbiting. Enable **Redraw static scene every frame** to measure continuous scene submission even when the camera is still. Disable it again for normal use; this comparison setting lasts only for this session.
5. Include the measurements and GPU model with a performance report.

For perspective, 2500 FPS is 0.40 ms per frame, 2000 FPS is 0.50 ms and 900 FPS is about 1.11 ms. Compare milliseconds to judge the added work. FORGE does not impose an FPS cap to hide that work.

## Find an error

Open Console and read the latest file/editor message, or runtime status. Compiler output is in Gameplay Code. Native compilation also writes its current complete log to `.forge/native/build.log`. The launchers retain console output on an editor failure.

## Runtime timing

While playing, Console shows **Tick** and **Fixed** (the simulation rate, normally 60 Hz). Tick counts completed gameplay updates, independently of editor FPS. **Dropped** counts whole ticks discarded during overload; **Clamped** shows elapsed seconds excluded after long stalls. These counters can explain lost simulation time without confusing it with render FPS. A fixed timestep alone does not guarantee deterministic gameplay.

## Report a problem

1. Open **Help → Copy build information** and paste it into your report.
2. Describe the action you took and what you expected to happen.
3. Include the Console message and a screenshot if the issue is visual.
4. Mention whether you were editing or playing and whether restarting changes the result.

The build format is `yymmdd-counter`, using UTC. The counter keeps increasing across dates. Failed packaging attempts may leave gaps. Re-downloading a build does not change its identity.
