# Authored animation graphs

An `animation_graph` root asset owns a `.animgraph.json` source using
`forge.animation-graph` version1. The bounded pose DAG reuses AnimationRuntime,
the pinned native sampler, exact clip selection, existing masks, Source documents,
publication, resource pools and runtime package closure. See [ADR035](decisions/035-animation-graphs.md)
and the [user guide](../manual/editor/animation-graphs.md).

## Source and identity

The source contains `asset_id`, nullable `skeleton`, `cycle_seconds`, `inputs`,
`nodes` and nullable `output`. A Clip node has `id`, `name`, `kind: "clip"` and
nullable `clip`. Blend/Additive nodes have `first`, `second`, nullable `mask` and
`weight`. The latter is a constant or `{input:slot,scale:number,bias:number}`.
Each input has a stable integer `slot`1–4, `name` and `initial`.

Node UUIDs, ports, output and source-layout positions are graph-local identities.
They are never catalog references. Typed reference inspection visits only skeleton,
clip and mask bindings, including when a local UUID happens to equal an asset UUID.
Copy changes the root asset identity while retaining local UUIDs and unknown
extensions. Move preserves the source document and requires the normal suffix.

Recognized `editor_layout` uses `forge.animation-graph-layout` version1 with
`positions:[{node:localUUID,x:number,y:number}]`. Nodes must exist, positions are
unique, and coordinates are finite within±1,000,000. Unknown layout formats remain
opaque and cannot be overwritten by native position edits. The existing canvas
uses transient numeric handles projected from UUIDs; handles are never serialized.
Port1/2, drag, arrangement and removal use ordinary Source edits/history.

Drafts allow null bindings and disconnected nodes; present links must resolve and
all subgraphs must be acyclic. Publication requires a complete, reachable output.
Topological compilation follows links independently of authored row order. Cooking
strips layout and opaque metadata. Source edits validate their formatted saved
representation before entering history.

## Publication, resources and evaluation

Importer `forge.animation.graph`, recipe `forge.animation.graph.v1`, publishes
`graph.json` as `forge.animation-graph` version1. Runtime roles are `graph.skeleton`,
`graph.clip:<localUUID>` and `graph.mask:<localUUID>`. Shared clip/mask bindings
must agree on exact revisions. `forge.animation_graph` records version, file,
byte count and SHA-256. Definition, recipe, provenance, file and typed edge sets
are admitted together. A selected read opens the existing cache without creating
missing cache directories.

The clip selector supports imported clips and target-bound baked clips for the
exact skeleton/model. Shared mask admission checks the same published weights and
skeleton revision in authoring, playback and export. Graph leases, clips and masks
remain owned by existing typed pools; native slots are not persisted as identities.

Profile1 permits32 nodes,8 clip occurrences,4 inputs and256 joints; source limits
are256KiB,12,000 JSON parser events and depth12. Cycle range is0.0001–3600 seconds.
Finite input values/scale/bias stay within±1,000,000; effective weights clamp to[0,1].
Graph node/joint work participates in the existing8,192 evaluated-joint and256-player
world limits. These are admission bounds, not measured hardware performance.

All leaves follow one normalized cycle. Shared native leaves sample once, bounded
owned local-pose scratch feeds one final model pass, and the existing sampler owns
blend/additive math. Morph defaults and transform-channel unions include every leaf.
Masks, complete pose validation, writer conflicts and rig constraints retain their
existing owners. [ADR018](decisions/018-animation-root-motion.md) governs per-layer
interval root deltas; absolute blended-pose differences are not locomotion.

## Controls, preview and recovery

Animator adds a Graph reference and four `graph_override_N`/`graph_input_N` pairs.
Unchecked overrides use source defaults. Graph mode explicitly rejects other
clip/state/space/manual selections. Input changes reuse admitted samplers and leases,
preserve clocks and validate the full candidate pose before publication. Failed
inputs or poses preserve the working configuration and pose.

Stopped previews and live Play input tuning call the same animation-owned candidate
transaction. Live tuning permits only declared input/override fields, with strict
number/Boolean types. Assets and playback-mode assignment remain authored operations.
RuntimeWorld, both Play hosts, Inspector and MCP `play.tune` share this routing and
the existing generation checks. Post-Stop review uses ordinary Scene history.

Private recovery9 wraps the preceding animation profile1–8, retaining profile8's
existing `sequence_profile`. Graph key, clip/mask keys, cycle, effective inputs,
override flags and stored values must match before recovery writes. Graph resources
join the existing recovery deadline; sampling scratch is rebuilt, not serialized.

Animator's native layout changes require matching SDK fingerprints and rebuilt
modules. Kits advertise `runtime_animation_graph: 1`; older kits reject Graph export.
Packaging validates the native graph/clip/mask/target closure and omits authored
sources. Gallery180 reuses the independently authored rig and motion from179, with
a new mask, four-node graph and comparison Scene.

## Automation

The `animation_graph` Source owner supports the existing document template/create/
open/read/patch/save/status/undo/redo/close operations. Discovery is
`asset.animation_graph.catalog`; MCP exposes `forge_asset_animation_graph_catalog`.
Source sessions/revisions, permissions, publication locks, history and failures
come from existing owners. Scene assignment uses `property.set_many`, and live
input changes use `play.tune`. The asset CLI discovers and imports `.animgraph.json`
through that same importer.

## Capture provenance and acceptance scope

`manual/images/animation-graph.png` is the production AnimationGraphEditor displaying
Gallery180's actual published source, rendered with the shipped font and native
ImGui draw data through the SDL CPU software renderer at100% UI scale. The native
control fixture also drives Source, stopped preview, live Inspector and post-Stop
review at100%,150%,200%, wide/narrow. Input frames compute actual native layout;
the retained display captures are rasterized. The source capture does not modify
the document or replace controls with artwork.

`manual/images/animation-graph-gallery.png` is a lossless conversion of Gallery180's
fixed-tick30 RuntimeWorld/FrameRenderer Game capture. The software Vulkan fixture
checks three animated landscape frames and a portrait frame, clear display borders
and exact pixels after relocating Windows-target content with its source project
offline. The graph reuses the existing FORGE-authored articulated model and motion.
The image is not retouched. These captures do not establish full Main, Windows,
physical GPU, physical DPI or graphical executable acceptance.
