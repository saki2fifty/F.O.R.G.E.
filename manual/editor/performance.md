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

## Inspect Play CPU costs

Open **Window → Profiler**, choose **Play runtime → CPU scopes**, and start collection.
Use **RuntimeEntityPublication** to see time spent creating queued objects and
Prefab groups. **ResourceSynchronization** shows resource synchronization;
**PresentationPoseCapture** shows the work that prepares smooth presentation poses.
**FixedSimulationTick** includes these stages and the rest of the simulation.

Use the same scene and actions when comparing runs. The profiler keeps recent
samples, so capture a creation spike soon after it happens. These are CPU times;
the rows overlap and should not be added together to calculate FPS. Stop collection
to keep the current samples, or Clear to begin another comparison.

## Inspect a CPU timeline

1. Open **Window → Profiler** and choose **Editor** or **Play runtime → CPU scopes**. Start Play first when inspecting its runtime.
2. Click **Start collection**, perform the action you want to measure, then click **Stop collection** to retain the recent records.
3. Enable **Timeline**. Read **Start ms**, **Duration ms** and the interval bars to compare completed measured scopes. Hover a bar for its name, tick and record sequence. Use the filter to find a scope; scroll the panel or table when needed.
4. Click **Copy trace JSON** to copy all retained measured intervals for that owner. Paste into a text file and save it with a `.json` extension for a viewer that supports Trace Event JSON. Copying includes the capture date, time and UTC offset. Filtering changes the display, not the copied trace.
5. Disable **Timeline** to return to the grouped summary. **Clear samples** starts a fresh observation window and discards unfinished samples. Clearing Editor samples also clears its GPU records.

The history holds at most 512 records. Nested scopes overlap; adding their durations does not give a frame time. Start times are relative to the earliest retained measured interval. The capture date identifies when the trace was copied, not an absolute timestamp for every interval. Editor and Play use separate clocks and should be inspected separately.

The timeline shows completed host scopes with measured starts. Duration-only records, including C++ SDK profile samples and the exclusive **UpdateAndEditorUI** total, stay in the summary. If the timeline is empty, collect an editor frame or a Play fixed tick and check its measured interval count. **Copy trace JSON** is disabled until a measured interval exists.

This view measures one owner thread. Trace files may use logical overlap lanes to lay out crossing intervals; these are not additional measured worker threads. GPU durations and ECS system lifetime totals remain in their own views. The timeline does not trace every ECS system invocation or show sampled call stacks.

## Inspect ECS system order

1. Start Play and open **Window → Profiler → Play runtime → ECS systems**.
2. Click **Start collection**. Run the game, or use Step while paused, to populate the table.
3. Read **Order**, **System** and **Phase** to inspect the native fixed-pipeline plan. **Merge** marks a Flecs synchronization point. Systems with no matching entities are absent.
4. Use **Filter system or phase...** to find a system. Order numbers keep their original positions when filtering; merge rows remain visible.
5. Stop collection to retain the view. **Clear samples** discards it; another collected fixed tick can populate it again.

**CPU lifetime ms** is the native system's cumulative CPU wall time since that system
was created, including native measurements before collection started. Clear does
not reset those counters. These values are separate from the recent **CPU scopes**
samples and do not measure one tick, a rendered frame or GPU time. **Mode** shows
native Immediate, Deferred or Multi-threaded configuration, not actual processor
utilization. A missing timing value appears as `--`.

The displayed tick identifies when the native plan was captured. Watching the
panel requests a new observation on the next collected fixed tick; inspecting a
paused game does not advance it. The plan can change as systems gain or lose
matches. It is not a log of every invocation or a thread timeline. Large plans
show their first 256 entries with a truncation notice; long names end with `...`.
Collection adds overhead. The authoring world's separate ECS inspection window
does not execute gameplay to produce these observations.

## Find an error

Open Console and read the latest file/editor message, or runtime status. Compiler output is in Build. Native compilation also writes its current complete log to `.forge/sdk-build/build.log`. The launchers retain console output on an editor failure.

## Runtime timing

While playing, Console shows **Tick** and **Fixed** (the simulation rate, normally 60 Hz). Tick counts completed gameplay updates, independently of editor FPS. **Dropped** counts whole ticks discarded during overload; **Clamped** shows elapsed seconds excluded after long stalls. These counters can explain lost simulation time without confusing it with render FPS. A fixed timestep alone does not guarantee deterministic gameplay.

## Report a problem

1. Open **Help → Copy build information** and paste it into your report.
2. Describe the action you took and what you expected to happen.
3. Include the Console message and a screenshot if the issue is visual.
4. Mention whether you were editing or playing and whether restarting changes the result.

The build format is `yymmdd-counter`, using UTC. The counter keeps increasing across dates. Failed packaging attempts may leave gaps. Re-downloading a build does not change its identity.

## When the GPU falls behind

FORGE limits queued rendering to two frames, even with VSync off. A slow GPU makes
the editor wait for completed work instead of continually allocating more graphics
resources. If frame completion stalls for five seconds, FORGE reports a rendering
error. This does not limit FPS to your display’s refresh rate.

## GPU pass timing

Open **Window → Profiler → Graphics GPU** and click **Start collection**. Use the
Scene or Game view, then stop collection to inspect recent pass durations. This
includes editor UI and asset previews. Collection starts off each time FORGE opens.

Results arrive after rendering is submitted. **Frame** identifies the submitted
editor frame; pending, skipped and discarded counts explain incomplete samples.
A retained Scene image does no new drawing. Use the existing redraw comparison
when you need continuous Scene work. These rows do not measure standalone game FPS
or total graphics card usage. GPU profiling can add overhead.

CPU and GPU collection are independent. Clearing editor samples clears both views;
closing the panel keeps collection running. Switching projects stops GPU
collection and keeps completed records until you clear them. A device without
supported GPU timing shows an explanation and continues rendering normally.
