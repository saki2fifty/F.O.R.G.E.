# One-dimensional animation blend spaces

`AnimationBlendSpaceAsset` places compatible imported clips on one numeric axis.
Animator's nullable `blend_space` selects the asset; finite `blend_position` selects
the two neighboring samples and their linear influence. Exact points and positions
beyond the ends use one endpoint. Null preserves direct and named-state behavior.
Older scenes default to null/0 without source rewriting. The public Animator
layout changes the exact SDK fingerprint; matching C++ modules must rebuild, while
ABI1 remains unchanged.

## Source, publication and identity

`.blendspace.json` contains format `forge.animation-blend-space`, version1,
`asset_id`, nullable draft `skeleton`, `cycle_seconds` and `samples`. Each sample
has a durable UUID `id`, nullable draft `clip` and numeric `position`. Up to64
samples and256KiB source are admitted. Positions are finite within±1000000 and
unique; cycle duration is finite within0.0001–3600 seconds. Repeated clips at
distinct positions are allowed. Cooking requires an imported skeleton, at least
one sample and complete clips from the same exact model revision/generation.
Sorting by position makes source row order irrelevant to playback/cooked bytes.
Names, display ordering and glTF indices never bind samples or clips.

Existing AssetSourceDocument owns edits/history; import service, AssetPublisher,
derived cache and catalog/dependency graph own publication. The trusted CPU
importer `forge.animation.blend-space`, recipev1, publishes immutable
`blendspace.json`. Runtime edges are `space.skeleton` and `space.clip:<sample UUID>`;
all carry the exact model revision. Failed drafts/cooks retain last-good output.
Move preserves identity; duplicate changes the asset UUID while preserving the
document-local sample IDs and clip references. Reference-impact review tracks
skeleton/clip references and leaves unknown extension fields uninspected.

## Playback and transactions

AnimationRuntime's existing player owns the cycle clock. A typed ResourcePool is
bounded to64 selections /4MiB. Binding checks recipe, source/artifact digests,
dependency closure and exact model/skeleton/clip selections. All admitted clip
leases and their combined transform/morph channel definitions remain cached;
sampling uses at most two active clips. Each clip samples `cycle time / cycle
duration`. Position changes within an admitted pair retain its samplers and all
bindings; crossing a sample reuses cached clip leases and channel definitions.
This normalized phase maps to each selected clip's own duration, including morph
curves. Channel union returns bones
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
pair/constant weights before the fixed interval, scale that interval into each
clip and blend per-layer deltas. Pair changes do not inject a pose jump into motion.
Apply/Extract neutralize the selected root only after raw capture. Paused edits,
preview selection and presentation do not emit gameplay motion/events. Shared
cycle Loop/Play/Pause/Restart/Seek and speed use existing playback controls.

## Preview, recovery and export

The existing copied-world Animation Preview exposes temporary position, cycle
scrubbing, loop and speed. Applying its settings uses one existing Scene transaction;
preview does not change authored source or execute project C++ systems. Exact
recovery records asset/revision, numeric position, cycle duration, stable selected
sample IDs and existing clip/blend clocks. Mismatched selections fail before clock
adoption. Legacy checkpoints without a space remain readable.

Runtime packaging collects typed dependencies, validates native model selections
and ships cooked space/model data. Gallery43 compares three positions in the
original FORGE column; source-free relocated export needs no glTF or source JSON.
Native Windows fixtures and physical acceptance are separate from local validation.

Concept reference only: [Godot's BlendSpace1D documentation](https://docs.godotengine.org/en/stable/classes/class_animationnodeblendspace1d.html).
FORGE uses original source and its existing pinned Ozz sampling/blending dependency.
