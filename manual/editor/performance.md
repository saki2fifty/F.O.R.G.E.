# Performance and diagnostics

The permanent bottom bar helps you see how the editor is running. Console explains recent operations and failures.

## Read the counters

**FPS** and mean frame time describe the editor's frame loop. Lower frame time means more frames per second. These are not GPU execution-time measurements.

**CPU** is the editor process's usage normalized across logical processors. **RAM** is its resident memory in MiB. Separate gameplay/compiler processes are excluded. Counters update about every half second; unavailable values show `--`.

**VSync off** means the application requests unsynchronized presentation and imposes no FPS cap. Driver or compositor settings can still affect observed rates. The bar also shows Edit/Play state and authored entity count.

## Find an error

Open Console and read the latest file/editor message, runtime status, or compiler output. Native compilation also writes its current complete log to `.forge/native/build.log`. The launchers retain console output on an editor failure.

## Report a problem

1. Open **Help → Copy build information** and paste it into your report.
2. Describe the action you took and what you expected to happen.
3. Include the Console message and a screenshot if the issue is visual.
4. Mention whether you were editing or playing and whether restarting changes the result.

The build format is `yymmdd-counter`, using UTC. The counter keeps increasing across dates. Failed packaging attempts may leave gaps. Re-downloading a build does not change its identity.
