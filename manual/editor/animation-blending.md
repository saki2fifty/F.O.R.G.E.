# Preview and blend two animations

An object's **Animator** can mix two skeletal clips and smoothly change between
them. Both clips must come from the same skeleton. You can use imported character
clips or try the included skinned column.

## Try the Gallery

1. Open `FeatureGallery / Scenes / 09-animation-blending.scene.json`.
2. Press **Play**. The left column uses **Idle**, the center mixes **Idle / Reach**, and the right uses **Reach**. Stop when you are ready to edit.
3. Select the center column's root, **Half Idle / half Reach**. In Inspector's **Animator**, inspect **Clip**, **Blend Clip**, **Blend Weight** and **Transition Seconds**. Weight `0` uses the primary clip; `1` uses the second; `.5` mixes them equally. Both clips move through the same percentage of their duration, even when one clip is shorter.
4. In **Content → Create / Register**, choose **Preview skeletal animation...**. The central **Animation Preview** document opens with the selected object.
5. Press **Play preview**, then **Pause preview**. Scrub **Playhead (seconds)**. Set **Transition (seconds)** to `0` and adjust **Blend target** to compare poses.
6. Set Transition to `.5`, resume preview and move Blend target between `0` and `1`. The mix changes smoothly from its current value. Changing the target halfway through a transition starts the next transition from that current mix.
7. **Stop preview** restores the authored Scene view. **Apply preview settings to Animator** stores these settings in the Scene as one Undo step; save the scene to keep them. Preview itself does not change the scene, Undo or physics.

Entering game Play, closing the document, editing the scene, changing projects or
removing the selected object or publishing updated imported assets ends preview.
Choose primary and secondary clips in Inspector; the preview document tunes their mix and timing.

## Control it from C++

Set a new blend target on the existing Animator. FORGE advances the transition
in the simulation; your code does not need to interpolate poses each frame.

```cpp
#include <flecs.h>
#include <forge/animation_components.hpp>

void set_reaching(flecs::entity actor, bool reaching) {
    auto animator = actor.get<forge::Animator>();
    animator.blend_weight = reaching ? 1.f : 0.f;
    animator.transition_seconds = .5f;
    actor.set(animator);
}
```

Assign both clips in Inspector before using this function. Call it from a C++
System when the desired action changes. Gameplay still uses C++ only. Rebuild your
project's native module with the SDK matching this editor after updating FORGE.

## Play and export

An authored blend weight takes effect immediately when Play starts. Later C++
target changes use Transition Seconds. Pause and speed `0` hold phase and mix;
Step advances one simulation tick. Loop repeats the primary clip's phase. With
Loop off, playback holds its final phase and mix when the primary clip finishes.

Save the Scene and export normally. Both referenced clips are included as cooked
data, and the standalone game plays the same blend without the source glTF,
editor or compiler. Clips with morph-weight curves currently use single-clip
playback; FORGE diagnoses them when selected for this two-clip skeletal blend.
