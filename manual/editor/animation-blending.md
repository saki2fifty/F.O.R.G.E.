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

## Blend part of a character

Open **Scenes / 37-animation-subtree-blending.scene.json** in the Gallery and
press **Play**. The left column uses the base Idle clip. The middle applies Reach
to **Upper joint** and its children while keeping the lower part on Idle. The
right applies Reach to the whole skeleton, including its movement and turn.

Stop, select **Upper subtree - Reach**, and find **Animator → Blend subtree**.
Choose a joint from the same imported rig to restrict the second clip to that
joint and its children. **None** blends the whole skeleton. **Blend Weight** and
**Transition Seconds** work as before. Choose both clips before selecting a
subtree; clear the subtree before removing the second clip.

Preview, Play and export use the same selection. With root motion enabled, an
upper-only selection keeps the primary clip's root movement. The filter also applies
to animated morph nodes: a mesh outside that subtree keeps its base expression.

## Reuse weighted bone masks

Open **Scenes / 42-animation-bone-masks.scene.json** and press **Play**. The left
column blends its whole skeleton. The middle blends only its upper branch. The
right uses different influences for the root, upper joint and tip, with a smaller
shape-animation influence. Stop to edit the mask.

1. In Content, open **Assets / Weighted.mask.json**. The **Animation Mask** document shows its skeleton, default influence and bone overrides.
2. Select a bone row. Change **Bone influence** and press Enter. `0` keeps the first clip; `1` gives the second clip its full **Blend Weight**; `.5` gives it half that influence.
3. Enable **Include descendants** to pass the influence to children. A child's own override takes priority. With the option off, only that bone changes; its children keep the earlier inherited or default influence.
4. Use **Save / Publish**, then Play again. Undo/Redo applies to the mask document. Failed publication keeps the previous working version.
5. To create your own, choose **Content → Create / Register → New animation mask...**. Choose an imported skeleton, use **New bone → Add bone**, and save the mask. Select an object with that skeleton and two clips, then use **Attach to selected Animator** or Inspector's **Bone mask** picker. Save the scene.

**Default influence** applies where no descendant-scoped override has supplied a
weight. **Blend subtree** still restricts the result. The mask works with both
Blend and Additive modes, including root movement and animated shapes. Changing a
mask does not restart animation time. A mask is tied to its imported skeleton;
rebuild it after that model's selected content revision changes. Missing bones are
reported instead of silently selecting another bone. Exported games include the
compiled mask and its required assets.

## Add a motion over another motion

Open **Scenes / 38-animation-additive-overlays.scene.json** and press **Play**.
The left column shows the base motion. The middle blends fully to the second clip.
The right keeps the base and adds the second clip's movement and deformation.

Stop, select **Additive - overlay**, and find **Animator → Blend mode**. Choose
**Additive** to add the second clip's changes relative to the imported rest pose.
Choose **Blend** to mix the two poses normally. **Blend Weight**, **Transition
Seconds** and **Blend subtree** control either mode. Both clips keep their timing
when you change the mode. The Animation Preview document identifies the selected
mode and uses the same calculation.

## Blend animated shapes

Imported morph animations can change mesh shapes, such as facial expressions.
FORGE blends these curves along with bone animation. Signed weights and values
above1 are preserved when the source uses them.

Open **Scenes / 39-animation-morph-states.scene.json**, then press **Play**. Three
columns start in different expressions and automatically fade through Base,
Overlay and Rest. Rest has no shape curve, so it returns to the model's default
shape. Stop and open **MorphMix.states.json** in Content to edit its states, fade
times and exits. Preview and exported games use the same expressions.

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
The Gallery's **CodeExamples / SetBoneMask.hpp** shows a typed C++ helper for
assigning or clearing an already-authored mask at a fixed tick.

## Play and export

An authored blend weight takes effect immediately when Play starts. Later C++
target changes use Transition Seconds. Pause and speed `0` hold phase and mix;
Step advances one simulation tick. Loop repeats the primary clip's phase. With
Loop off, playback holds its final phase and mix when the primary clip finishes.

Save the Scene and export normally. Both referenced clips are included as cooked
data, and the standalone game plays the same bone and morph blend without the
source glTF, editor or compiler.

## Blend clips along a numeric axis

Open **FeatureGallery / Scenes / 43-animation-blend-spaces.scene.json** to compare
three numeric positions. Its original column has Rest, Base and Overlay clips.
Select a model holder and change **Animator / Blend position**, or open
**Assets / Motion.blendspace.json** to edit the samples on its axis.

For your own imported model:

1. In **Content / Create / Register**, choose **New animation blend space**.
2. Choose the imported **Skeleton**, then **Add sample** for each clip. Give each sample a unique position, such as Idle at0, Walk at1 and Run at2. Clips must belong to the same imported model and skeleton.
3. Set **Cycle (seconds)** and **Save / Publish**. All clips share this cycle even when their original durations differ.
4. Select the model's Animator and click **Attach to selected Animator**. This changes the scene in one Undo step and replaces any named states or manual blend settings.
5. Click **Open Animation Preview**. Change its temporary **Blend position**, scrub or play, then use **Apply preview settings to Animator** when ready to save that position.

Between samples, Forge mixes the two neighboring clips. Beyond either end it uses
the nearest endpoint. Changing position preserves cycle phase, including while
paused. **CodeExamples / SetBlendPosition.hpp** shows ordinary C++ control. Save
and export normally; the game receives the compiled code and cooked animation
assets, and needs no editor or compiler to run.

## Blend clips with two controls

Open **FeatureGallery / Scenes / 65-two-dimensional-blend-spaces.scene.json**.
Its five original columns show the Rest, X and Y corners, a three-clip interior,
and a position in the other triangle. Press Play, or select a holder and use
Animation Preview to try **Blend position X** and **Blend position Y**.

To author your own, choose **Dimensions / 2D plane** when creating a blend space. Place
each compatible clip at an X/Y point, then Save / Publish and attach it to the
model's Animator. The plane shows the triangles used for blending. Dragging a
point commits one Undo step on release; Escape cancels the drag. Numeric X/Y
fields are also available. An existing one-axis source can be converted explicitly
with **Convert to 2D**; Undo restores its original format and values.

Inside each triangle, up to three clips contribute. Beyond the plane's boundary,
the position uses its nearest boundary point. A line of samples behaves as a
line blend. All clips retain one shared cycle phase as either control changes.
Preview changes stay temporary until **Apply preview settings to Animator**,
which saves both controls as one Undo step. Live Play edits stay runtime-only.

`forge_set_blend_position(actor, x, y)` in **CodeExamples / SetBlendPosition.hpp**
sets both controls together from C++. After this FORGE update, rebuild project
modules with its matching SDK because Animator gained the second coordinate.
Export ships cooked blend/model data and your compiled module.
