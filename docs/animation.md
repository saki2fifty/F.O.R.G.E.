# Skeletal animation foundation

FORGE uses unmodified **Ozz Animation 0.17.0**, commit
`744eb9d99f606eda849acb0b1204f7a3dc20bca1`, as a private runtime implementation.
The runtime and official `gltf2ozz` converter are built from the same pin.
Ozz provides compressed runtime skeletons/clips, `SamplingJob`, reusable sampling
contexts, and `LocalToModelJob`. FORGE supplies asset admission, provenance,
publication, ECS authoring, clocks, recovery, and debug visualization.

## Ownership and clocks

`forge.animation` registers the optional `forge.animator` schema in authoring and
validation worlds without loading Ozz assets. Its runtime provider owns an
legacy AssetId cache and typed cooked-model resources described below. Each live Animator owns its
sampling context and transient playback state. The legacy bridge limits are256 players,
8192 evaluated joints,64 cached asset identities and64MiB of admitted archive data
(the Ozz objects/caches add bounded working memory). Bones are not Flecs entities.
The exact native SDK exposes the engine-owned Animator component; it exposes no
Ozz headers, context pointers, or broad animation service. ABI1 is unchanged.

Animator fields are `skeleton`, `clip`, `enabled`, `play_on_start`, `loop`, and
`playback_speed` (0–4). Asset refs are nullable typed identities. Existing property
validation, independent prefab overrides (including equal values), source
publication, Revert, duplication, persistence, and scene Undo apply normally.
Changing Play on Start after realization does not restart playback. A clip or
skeleton reference change realizes a fresh player after successful admission.

The existing runtime fixed clock advances authoritative clip time after gameplay
writes. Pause makes no ticks; Step advances exactly one tick. Presentation samples
Ozz at an interpolated **time**, including loop wrapping; it never interpolates
model-space matrices. Reset/recovery invalidates sampling caches and removes stale
presentation history. Stop tears down runtime animation state. The editor's debug
bone overlay uses the existing viewport camera projection and entity world affine.
It is an unoccluded diagnostic overlay, not mesh skinning.

Recovery extends the private version1 recovery envelope with a versioned
`animation` section: EntityRef, SkeletonAssetId, ClipAssetId, exact artifact
revisions, time, and playing state. It stores no pointers, contexts or matrices.
Candidate-world reconstruction re-admits assets, checks exact bindings/revisions,
recreates contexts, samples the pose, and only then replaces the old world. Old
checkpoints without animation are accepted only for worlds without realized
Animators. Existing physics reconstruction and integrity checks remain intact.

## Supported source and conversion path

`prepare_animation_conversion` runs off the editor thread; `AnimationCandidate::publish`
runs on the owning project thread after writer ownership is checked. The application
supplies an absolute packaged converter path; no PATH search or shell is used.

The initial path accepts core glTF2 `.gltf`, 1–1024 nodes, 1–8 uniquely named clips,
and up to16 buffers. Only skeletal translation/rotation/scale channels are admitted;
morph weights and non-transform channels are rejected. Source JSON is limited to4MiB and nesting to64 levels. Embedded
base64 or project-contained relative buffer files are supported, with16MiB total
source/dependency staging. Network/absolute/escaping/percent-encoded dependencies,
extensions and image assets are rejected. This is an animation-only bridge, not
universal glTF import. FBX is unsupported. Model/mesh/material/image import uses the separate
[model pipeline](model-import.md), not this legacy bridge.
The owned fixtures cover translation and quaternion rotation on a two-joint hierarchy;
broader DCC export compatibility requires additional fixtures.

The process receives fixed filenames in a unique staging directory. Windows Job
Objects bound process memory to512MiB, forbid child processes, and terminate the
worker on job closure. POSIX applies address-space/file/CPU limits and owns a process
group. Both enforce cancellation, a30-second wall deadline and output monitoring;
each admitted output is at most16MiB and total staging is checked against32MiB.
Windows output monitoring is polling, not a disk quota. Tool failure never publishes.

Settings are runtime skeleton/animation output, no additive conversion, no root-motion
extraction,30Hz sampling, optimization enabled, and **iframe_interval=0**.
Generated filenames are FORGE-controlled, never derived from clip names.

## Exact archive admission subset

`src/animation_archive.cpp` is deliberately version-coupled to this Ozz revision:

- Little-endian runtime Skeleton archive version2 and Animation version7 only.
- Exactly one object per file; correct tag/version and no trailing data.
- 1–1024 joints/tracks; nonempty NUL-terminated skeleton joint names, at most255 bytes
  each, exact name-table consumption, bounded allocation arithmetic.
- Parents precede children, have valid indices and form depth-first ordered forests.
- Finite bounded rest transforms and normalized quaternions.
- 2–65535 strictly increasing timepoints spanning0–1, with at least1e-8 separation.
- Duration0.0001–3600seconds; per-channel keys between twice padded track count and262144.
- All ratio indices, predecessor offsets, initial key sets, per-track predecessor
  chains, global ordering and terminal keys are checked before load.
- Finite half-float channels, valid packed smallest-three quaternion bounds,
  largest-component consistency and continuous quaternion hemispheres.
- Optional compressed seek iframe arrays are rejected. The disabled builder writes
  empty arrays and interval1; that exact representation is required.

The validator allocates only bounded arrays after count checks and never constructs
an Ozz object. `Skeleton`/`Clip` constructors validate first and deserialize the
**same byte span** using a read-only bounded stream. No pathname is reopened between
validation and Ozz load. The stream is additional defense, not a substitute parser.
All returned sampled model matrices must be finite. An unsupported numeric pose is
reported as an animation failure, not silently applied to authored transforms.

This is not support for arbitrary `.ozz` archives. Generic raw `.ozz` import is not
exposed. The supported subset is defined by converter configuration **and** validator.

## Provenance and candidate publication

The existing version1 AssetCatalog gains an optional preserved `metadata` object;
there is no second asset database. Animation metadata version1 records:

- Persistent source AssetId (its current locator lives in the source catalog record).
- Generated skeleton/clip AssetIds in catalog records and dependencies.
- Ozz version/revision, converter name/revision and actual executable SHA-256.
- Exact converter settings and source/dependency SHA-256 digests.
- Generated artifact SHA-256, clip name, SkeletonAssetId and exact skeleton SHA-256.

SHA-256 is content identity/change detection, **not** authentication or signing.
Runtime compatibility requires the exact skeleton identity/content revision and
matching conversion provenance; equal joint counts alone are insufficient.
An ID-aware source relocation preserves references. Repeated conversion keeps IDs.
Changing the clip-name set is currently rejected rather than silently retargeting
references. Reconvert the full set together; runtime Play must restart to select a
new revision. A private cache retains admitted versions for that runtime world.

Publication validates every artifact, proves Ozz loading/sampling, checks that the
source/catalog did not change, writes a unique immutable candidate directory, then
atomically selects the complete catalog last. Failure preserves the previous selected
set and metadata. Interrupted publication may leave unselected files for future cleanup;
it cannot make a partly written candidate active. Scene Undo does not undo asset conversion.

## Validation and upgrades

Permanent tests include the minimal26/27-byte audit regressions, every truncation of
the owned fixture, count/name/parent/key/index/time/NaN/infinity/version/iframe/trailing
rejections, deterministic mutations, numeric poses, source moves, stale candidates,
hash/binding mismatches, prefab intent, fixed clocks, and reconstructed recovery.
Sanitizer and platform results are recorded separately in the daily changelog;
implementation alone is not evidence of a completed platform check.

An Ozz upgrade requires re-reading upstream archive/load/sampling/builder code,
reviewing all allocation/index assumptions and converter defaults, updating the
validator/metadata compatibility intentionally, then rerunning rejection, mutation,
sanitizer, conversion and numeric sampling tests before the new pin is adopted.

## Official references

- [Pinned Ozz source](https://github.com/guillaumeblanc/ozz-animation/tree/744eb9d99f606eda849acb0b1204f7a3dc20bca1)
- [Ozz archive contract](https://github.com/guillaumeblanc/ozz-animation/blob/744eb9d99f606eda849acb0b1204f7a3dc20bca1/include/ozz/base/io/archive.h)
- [Ozz converter configuration](https://github.com/guillaumeblanc/ozz-animation/blob/744eb9d99f606eda849acb0b1204f7a3dc20bca1/src/animation/offline/tools/reference.json)
- [Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
- [SHA-256 specification, FIPS180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)

Deferred: root motion application, blending graphs, state machines, IK, retargeting,
skinned mesh/material rendering. The separate model importer/cooker is in progress;
Phase6E navigation is already implemented.

### Matrix rest transforms in the legacy bridge

Both animation-only conversion and full model conversion now share the same CPU
glTF transform admission and Ozz rest-input adapter. Matrix-authored rest transforms
are converted to explicit TRS only in private converter input. This preserves the
rest pose of an unanimated parent while its child animates: the pinned converter's
fallback channels read TRS even though its skeleton loader reads matrices.

Original source bytes remain unchanged. Non-affine matrices, non-TRS shear, singular
matrix columns, and simultaneous matrix plus TRS reject before launching the
converter. Explicit TRS still permits signed and zero scale within the animation
archive profile. No renderer, image codec or native model-loader dependency is
introduced into the animation-only bridge.

## Cooked model animation resources (Phase 7)

The model importer publishes Skeleton and AnimationClip members in the model's
immutable cooked family. Runtime animation now accepts these typed references as
well as the legacy animation-only bridge. Catalog dependency edges select the
clip's skeleton; the physical model container is not a second asset graph and is
not acquired as a logical dependency of its own members.

One subsystem-owned `ResourcePool<SkeletonAsset>` and one
`ResourcePool<AnimationClipAsset>` load admitted cooked bytes asynchronously.
Each has one preparation worker and bounds64 assets,64 requests and64MiB of
retained animation allocations. The existing256-player/8192-evaluated-joint limit
still applies. Whole-container read/validation scratch follows model admission
limits and is separate from retained resource memory. Source conversion, source
codecs and GPU access are absent. The original model source may be unavailable
while its previously selected cooked revision remains usable.

Resource revisions use the complete model build key and publication generation,
not just the raw Ozz archive digest. Skeleton order, bind data or companion morph
curves can change independently of archive bytes. The clip request depends on the
skeleton readiness ticket; a consumer pins both matching leases before replacing
its pair. Pending loads retry at owner synchronization boundaries, including
paused presentation. Ordinary ticks and presentation never wait for a worker.
Failed loads cannot substitute a mismatched pair or invalidate held prior leases.
Removed consumers release unused selections; shutdown destroys samplers before
closing pools. This does not yet implement source-watch adoption into active Play.

Sampling exposes local TRS copied directly from Ozz's sampled SoA lanes. It does
not decompose model matrices, so signed and zero local scale remain representable.
Companion morph curves use the same interpolated runtime time as the Ozz sample.
Node indices in this transient pose refer to the selected immutable model revision;
they are not persistent EntityIds. Model-node resources additionally retain durable
node AssetIds. The fixed runtime applies explicit source channels to matching model
instance nodes; see [model binding](model-import.md#fixed-runtime-node-animation).
Binding the resulting poses to rendered skins remains ongoing Phase7 integration.

Pending bindings yield a null recovery checkpoint instead of claiming a complete
snapshot. The runtime response retains the `recovery` field with that null value.
Once ready, the existing version1 animation recovery section records full family
revisions. Candidate-world recovery may wait at its unpublished, non-ticking
reconstruction boundary, with one30-second deadline for all model dependencies.
It checks exact revisions before publication. Ordinary playback never uses that
wait. No authored scene identity, Animator layout, SDK component layout or ABI1
change is introduced.

Retained memory counts the admitted native allocations and FORGE vectors, rather
than treating serialized archive length as native memory use. Exact Ozz0.17
`Skeleton::Allocate` supplies the skeleton layout; `Animation::size()` omits name
storage, which FORGE adds explicitly. Allocator bookkeeping and preparation scratch
are not presented as retained asset bytes.

## Model animation and physics admission

RuntimeSimulation installs an owner-thread host validator when both animation and
physics modules are present. Animation passes its complete candidate transform graph
through the existing PhysicsRuntime configuration checks before writing any animated
TRS channel. The callback holds a weak physics reference and is cleared on simulation
teardown; it adds no gameplay SDK service or ABI1 entry.

This reuses Jolt-backed collider scale/shape admission, effective spatial ancestry,
position/decomposition checks and Dynamic solver ownership. An animation cannot move
or rotate an existing Dynamic body by writing its transform; the same restriction
applies before first realization. Valid Static/Kinematic configurations keep their
existing supported parenting semantics. A rejected instance pose preserves all its
previous TRS values and the existing collider, emits an animation binding diagnostic,
and keeps recovery unavailable until the binding is repaired. Playback time continues
so a repaired binding can resume; no authored source or collider is silently rewritten.

Recovery uses the same validator while sampling into temporary state. Failure leaves
existing playback times and transforms unchanged. Complete runtime recovery still
reconstructs the scene and physics before admitting animation; the animation section
alone does not replace the scene or roll back solver state.
