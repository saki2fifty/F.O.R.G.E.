# See an animation's progress

During game Play, select an animated character in the Hierarchy. The Inspector's
**Live gameplay** panel now includes **Animation playback**.

- **State** identifies the current named animation, or shows Direct clip.
- **Time** and the progress bar show how far the current clip has reached.
- **Loop / One-shot** tells you whether the clip repeats.
- **Disabled**, **Held**, **Playback active** or **Clip finished** describes playback.
- **Incoming fade** shows a named-state transition; **Clip mix** shows direct blending.

This section is read-only. It uses simulation time, so Pause holds the progress
and Step moves it one simulation tick. Playback active means the player is ready
to advance when simulation resumes. A looping clip returns to the start of its
progress bar. A one-shot reaches100%; its fade may finish afterward.

## Return to Idle from C++

The Gallery includes an optional original **CodeExamples / OneShotReturn.hpp**
helper. It lets a C++ System return a character to Idle when a one-shot finishes,
without maintaining a separate timer. The CodeExamples README shows setup.

Use a writable Gallery copy. In **MarkerPulse.states.json**, turn off Loop for
Walk, save/publish, then give the Walk character state2. With the example helper
called from your C++ System, completion selects Idle state1. The normal Gallery
still runs without a compiler; adding this behavior requires a matching built
C++ module. Save your settings/scene and export with matching runtime/module kits.
The exported game carries compiled behavior and needs no compiler installed.
