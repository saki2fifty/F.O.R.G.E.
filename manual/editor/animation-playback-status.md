# See and control an animation's progress

During game Play, select an animated entity in the Hierarchy. The Inspector shows
a tinted **Live animation** section. These values apply to the running game.

- **Time** and the playhead show the incoming clip's position.
- **State**, Loop or One-shot, Speed and fade/mix status describe the current playback.
- **Pause clip / Resume clip** holds or resumes this character's animation.
- **Restart clip** returns it to the beginning and resumes the clip.
- Drag the playhead and release to **seek**. It keeps the clip's pause state.

Game Pause is independent. While the game is paused, seeking shows the new pose
without advancing the game clock. Step advances one simulation tick. Seeking
skips events and character movement across the jump; ordinary playback afterward
continues from the new position. Restart retains its usual beginning events.

These controls do not change the saved scene, Animator Enabled or Speed. Stop
returns to your authored scene. The exported game contains compiled behavior and
has no Inspector or editor playback controls.

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

## Play, pause or restart a character from C++

The Gallery's optional **CodeExamples / ClipControl.hpp** helper controls the
animated entity your C++ System is currently processing. You do not have to copy
its scene UUID into your code. The CodeExamples README shows how to connect your
project's existing input actions.

- **Play** resumes where the character stopped. It also starts a character whose Animator has **Play on Start** off.
- **Pause** freezes that character's animation while the game keeps running.
- **Restart** plays the current clip from the beginning. It ends an incoming state fade immediately. Use Restart to replay a finished one-shot.

Run these commands from a fixed Gameplay System. A System running after animation
has already updated, such as PostPhysics, cannot change playback that tick. Keep
Animator Enabled on and Speed above zero to see movement. Changing the selected
state still chooses that state's animation normally.

Use pressed edges for buttons; restarting every tick keeps returning to the start.
These runtime commands leave your saved scene settings unchanged. A recovered Play
session retains a character's paused/resumed state. The exported game includes the
compiled behavior and needs no compiler installed. This is C++ behavior; the live
Inspector playback controls are available only inside FORGE.

## Seek from C++

The optional Gallery **CodeExamples / ClipSeek.hpp** helper moves the current
entity's incoming clip to a position in seconds. Call it once from a Gameplay
System, using a pressed input edge. Seconds must fall between zero and the clip's
duration. For a looping clip, its end maps to the beginning.

Seek preserves pause and fades. It leaves an outgoing state's clock independent,
skips events across the jump, and does not move the entity across a root-motion
jump. A recovered session retains the sought position. Rebuild project code with
the matching SDK before Play or export.
