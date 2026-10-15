# Animation blend spaces

`AnimationBlendSpaceAsset` places compatible imported clips on one or two numeric
axes. Animator's nullable `blend_space` selects the asset; finite `blend_position`
and, for two axes, `blend_position_y` select the local samples and their influence.
One axis interpolates its neighboring pair, clamping beyond the ends. Two axes use
the prepared triangle containing the position and at most three clips. Null
preserves direct and named-state behavior. Older scenes default to null/0/0 without
source rewriting. The trailing Animator Y field deliberately changes the exact SDK
layout changes the exact SDK fingerprint; matching C++ modules must rebuild, while
ABI1 remains unchanged.

## Source, publication and identity

`.blendspace.json` contains format `forge.animation-blend-space`, version1 or2,
`asset_id`, nullable draft `skeleton`, `cycle_seconds` and `samples`. Each sample
has a durable UUID `id`, nullable draft `clip` and `position`: a number in version1,
an object with numeric `x` and `y` in version2. The source version explicitly selects
one or two dimensions; reading/saving a version1 document never upgrades it.
Unknown document/sample/position extension fields remain preserved. Up to64
samples and256KiB source are admitted. Positions are finite within±1000000 and
unique; cycle duration is finite within0.0001–3600 seconds. Repeated clips at
distinct positions are allowed. Cooking requires an imported skeleton, at least
one sample and complete clips from the same exact model revision/generation.
Sorting by coordinates makes source row order irrelevant to playback/cooked bytes.
Names, display ordering and glTF indices never bind samples or clips.

Existing AssetSourceDocument owns edits/history; import service, AssetPublisher,
derived cache and catalog/dependency graph own publication. The trusted CPU
importer `forge.animation.blend-space`, recipev2, publishes immutable
`blendspace.json` in cooked wrapper2 for either source version. Attributable recipe1/
wrapper1 selections remain readable for source1. Wrapper1 cannot contain source2.
The bare source1 `cook()` default retains its previous wrapper1 bytes; selected
imports check the recipe's exact wrapper version. No metadata identity is reminted.
Runtime edges are `space.skeleton` and `space.clip:<sample UUID>`;
all carry the exact model revision. Failed drafts/cooks retain last-good output.
Move preserves identity; duplicate changes the asset UUID while preserving the
document-local sample IDs and clip references. Reference-impact review tracks
skeleton/clip references and leaves unknown extension fields uninspected.

## Prepared two-axis selection

Admission constructs an original deterministic triangle mesh in normalized
coordinates. A triangle supplies nonnegative normalized barycentric weights;
exact vertices and edges omit zero-weight layers. Outside the convex hull the
position clamps to its nearest boundary segment. One sample selects that point;
two or collinear samples interpolate along their ordered segments. Equal-distance
ties are deterministic. Numerically unstable/incomplete geometry rejects admission
and retains last-good output. Derived points/triangles/edges count toward resident
resource bytes. Neither runtime selection nor the revision-cached editor plane
retriangulates per frame. There is no authored triangle topology or smooth/cubic
interpolation mode.

## Playback and transactions

AnimationRuntime's existing player owns the cycle clock. A typed ResourcePool is
bounded to64 selections /4MiB. Binding checks recipe, source/artifact digests,
dependency closure and exact model/skeleton/clip selections. All admitted clip
leases and their combined transform/morph channel definitions remain cached;
sampling uses at most two active clips in1D or three in2D. Each clip samples `cycle time / cycle
duration`. Position changes within an admitted pair retain its samplers and all
bindings; likewise,2D weight edits within one admitted triangle retain its samplers.
Crossing a sample/triangle reuses cached clip leases and channel definitions.
This normalized phase maps to each selected clip's own duration, including morph
curves. A single native Ozz blend/model pass composes the selected local poses.
Morph curves use the same normalized weights, including authored defaults for
clips without a curve. The shared8192 evaluated-joint budget counts every active
sampling layer at admission and recovery; the existing256-player bound remains.
Channel union returns bones
and morphs used by a previous pair to rest/default when the new pair omits them.
There is no second clock, animation manager, database or per-sample file import.

Position changes admit a complete candidate and preserve cycle phase/play state.
Explicit paused edits commit joint channels through the existing transform/physics
transaction and presentation cache snap. Physics rejection retains the prior
complete player/pose; invalid desired configuration cannot be checkpointed.
Published asset reloads adopt at a fixed boundary. Cycle-duration changes preserve
normalized phase before consuming the next interval. Named states and direct
second-clip/mask/subtree/Additive settings exclude this mode. Editor attachment
clears those settings atomically in one Scene history step; named-state attachment
clears the blend-space reference likewise.

Root motion follows [ADR018](decisions/018-animation-root-motion.md): choose the
pair/triangle and constant weights before the fixed interval, scale that interval
into each clip and blend per-layer deltas. In2D the stable selected-sample order
accumulates translation by normalized influence and rotation using the existing
shortest-path root slerp with each new weight divided by accumulated weight.
This explicitly ordered root policy is distinct from Ozz's normalized local-pose
quaternion blend. Selection changes do not inject a pose jump into motion.
Apply/Extract neutralize the selected root only after raw capture. Paused edits,
preview selection and presentation do not emit gameplay motion/events. Shared
cycle Loop/Play/Pause/Restart/Seek and speed use existing playback controls.

## Preview, recovery and export

The existing copied-world Animation Preview exposes temporary X/Y position, cycle
scrubbing, loop and speed. Applying its settings uses one existing Scene transaction;
preview does not change authored source or execute project C++ systems. Exact
recovery records asset/revision, numeric position, cycle duration, stable selected
sample IDs and existing clip/blend clocks. Two-axis entries additionally retain
dimension, Y coordinate and all selected sample IDs/weights. Mismatched selections fail before clock
adoption. Legacy checkpoints without a space remain readable.

Runtime packaging collects typed dependencies, validates native model selections
and ships cooked space/model data. Gallery43 compares three positions in the
original FORGE column; Gallery65 adds five two-axis positions, including two
three-way interiors. Source-free relocated export needs no glTF or source JSON.
Native Windows fixtures and physical acceptance are separate from local validation.

Concept references only: [Godot's BlendSpace1D documentation](https://docs.godotengine.org/en/stable/classes/class_animationnodeblendspace1d.html)
and [BlendSpace2D documentation](https://docs.godotengine.org/en/stable/classes/class_animationnodeblendspace2d.html).
FORGE uses original source and its existing pinned Ozz sampling/blending dependency.
