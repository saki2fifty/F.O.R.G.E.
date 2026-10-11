# Choose named animation states

Name your character's animations once, then choose them from C++. FORGE handles
the smooth change between clips. No additional scripting language is needed.

## Try the character Gallery

1. Open `FeatureGallery / Scenes / 10-animation-states.scene.json` and press **Play**. The three characters use Idle, Walk and Run. These are actual character animations. Stop to edit.
2. Select a character's root, such as **Idle - state 1**.
3. In **Content → Create / Register**, choose **Preview skeletal animation...**. Press **Play preview** and choose **Preview state** to change between Idle, Walk and Run. You can Pause and scrub the selected clip. Preview changes are temporary.
4. **Apply preview settings to Animator** stores the chosen state and speed as one Undo step. Save the scene to keep them.
5. To export, choose this saved scene as startup in **Project Settings**, save settings, then use **Run → Export Game...**. All clips in the state set ship already compiled/cooked with the game. The original GLB and compiler are unnecessary to run it.

The clip named Sprint in the source is named Run in this demonstration. These
animations keep the character in place. Your C++ movement code moves the object.
They come from Quaternius under CC0; the Gallery includes its license.

## Create or change a state set

1. Import a GLB or glTF character that contains its skeleton and animations.
2. In **Content → Create / Register**, choose **New animation states...**. Create a file such as `Assets/Character.states.json`.
3. In the central **Animation States** document, choose the imported **Skeleton**. Choose **New state clip** and press **Add state** for each animation.
4. Select each row. Set its **Name**, **Clip**, **Loop**, **State speed** and **Fade in (seconds)**. A fade of `.25` blends into that state over a quarter of a second. Zero changes immediately. Use clips from this exact character import.
5. Choose **Make entry state** for the starting animation. Reordering or renaming keeps each row's **Stable ID**. Removing a row invalidates code or scenes that use that ID.
6. Press **Save / Publish**. Select the character root with an Animator, then **Attach to selected Animator**. This assigns the set and its entry as one Scene Undo step. Save the scene.

To reopen the set, open its `.states.json` file from Content. Undo/Redo affect the
source document. Save/Publish applies changes to its cooked version. An invalid
set reports an error and keeps the previous published version.

Inspector's Animator **States** selects the set. **State** selects its stable ID;
`0` uses the entry. **Playback Speed** multiplies the state's speed; zero holds
its current pose and fade. The set owns clip, loop and fade settings. Clear its
States reference to return to ordinary direct-clip playback.

## Choose a state from C++

Use the IDs displayed by your set. In the supplied Gallery, `1` is Idle, `2` is
Walk and `3` is Run.

```cpp
#include <flecs.h>
#include <forge/animation_components.hpp>

void choose_locomotion(flecs::entity actor, bool moving, bool running) {
    auto animator = actor.get<forge::Animator>();
    animator.state = moving ? (running ? 3u : 2u) : 1u;
    actor.set(animator);
}
```

Call this from your normal C++ System. Repeatedly selecting the same state keeps
its playback time. Changing states starts the incoming clip at zero and fades
from the current animation. If another change happens halfway through a fade,
FORGE starts from the current pose. Pause holds the transition and Step advances
one simulation tick. A short nonlooping clip holds its last pose until the fade
finishes.

After an editor/SDK update, rebuild your C++ project with the matching SDK. State
sets select clips and fade timing; your C++ code decides when to switch. Layers,
retargeting and automatic condition graphs are not available in this feature.
