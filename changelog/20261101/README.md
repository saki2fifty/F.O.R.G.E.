# 2026-11-01

## Moving navigation obstacles

Navigation Obstacle adds an authored cylinder in 3D or circle in XY. An explicit
**Dynamic obstacles** bake profile lets agents reroute as objects move, disable
or disappear. Each query owns its derived tiles; shared published geometry stays
immutable. Existing static navigation assets and default bake settings retain
their formats and identities. Rebuilding a profile retains its logical asset ID.

Inspector, Scene/prefab persistence, history and automation/MCP use the existing
component and navigation owners. Gallery 129 and 130 contain original animated
examples, ready baked navigation and source-free export content. The manual
includes the controls, steps and finite limits. Public C++ headers change, so
modules must rebuild against the matching SDK; the C host table and dependency
pins are unchanged.

The profile supports eight enabled obstacles, eight touched layers per obstacle,
64 layers total and circular upright transforms. It does not add collision or
rebake moving source geometry. Windows, full editor and physical GPU acceptance
remain pending; this local batch does not produce a numbered Windows package.

## Source-aware Sprite atlases

An authored `.atlas.json` source now selects published images and packing settings
through Content. Save / Publish and input-source updates reuse the existing import,
dependency and isolated-worker pipeline. Each image keeps a stable local frame key
when packing moves its rectangle. Removing a frame retires its key; Undo preserves
the allocation sequence. Input failures retain the last working publication.

The packed Texture stores its bounded frame mapping alongside pixels in the same
immutable revision. Sprite **Texture frames** explicitly selects that mapping;
the default raw grid, crop and frame-list workflows remain available. The picker,
frame-local mip filtering and held frame animation use the selected revision.
Missing keys retain an existing complete draw and report a diagnostic. Export
contains the cooked atlas rather than its build-only image inputs.

The recipe editor shares source-document history, revision/session checks and
publication with automation and MCP. Reference inspection, Duplicate and Move
preserve its identity rules. Gallery 127 includes an editable atlas, six Sprites
and sparse-key animation; the manual includes authentic editor and Game captures.
Texture bundle output profile 2 is required for this mapping. Public Sprite/Texture
C++ layouts change, so native modules must rebuild against the matching SDK;
the C host table and dependency pins are unchanged. Physical Windows review and
a numbered Windows delivery remain pending.

## Tile Map decoded-chunk residency

Scene and Game camera views drive background preparation and release of dense
32×32 Tile chunks through the existing resource pool. Multiple views reuse
identical pages; complete previous Map/image selections remain while edited
visible chunks prepare. Loaded resources reports Tile chunks separately.
Presentation bounds decoded pages and aggregate worker-input backing separately.
Per-chunk request failures preserve prepared bindings instead of repeating work.

Runtime and published presentation keep complete sparse logical Map values.
Offscreen cells, edits, neighbor rules, physics, copied Play and SDK queries retain
exact behavior. Edited checkpoints charge their independent dense reconstruction.
Cooked admission, worker verification and package checks share strict sparse
validation; explicit full loading remains available. Root identities, cooked1,
public SDK layouts and dependency pins remain unchanged. Export keeps the complete
Map. This is bounded decoded presentation residency, with no unlimited disk-world,
collision-unload or image-bank-streaming claim.

Visible-cell pose admission shares its visitor with emission. Partly visible dense
chunks can fit small pose budgets; oversized views reject and recover on a fitting
view. Local software-Vulkan checks are distinct from queued physical Windows/GPU
acceptance. No numbered Windows package accompanies this local batch.

Gallery126 supplies an original five-region Map and a looping camera Clip. It adds
one Scene and two authored assets through the existing document/publication owners,
preserving all 336 prior records and project settings. The manual includes current
loading/export steps and an authentic Loaded Resources component capture.
