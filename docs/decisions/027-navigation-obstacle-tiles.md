# ADR027 — Derived obstacle tiles in the navigation owner

Date:2026-11-01. Accepted; implemented and locally validated. Windows and physical
platform acceptance remain pending.

## Decision

Extend ADR026 with an explicit `dynamic_obstacles` bake profile. The default static
profile, one logical asset/profile per Scene, source capture, immutable publication, and
the Spatial3D/PlanarXY coordinate boundary remain authoritative. This profile carves
authored moving cylinders into baked heightfield tiles; it does not rebake moving geometry
or provide physics collision.

NavigationCandidate and its worker produce bounded FORGE raw-cell layers in a new version
4 envelope. Geometry provenance remains version2 for spatial geometry or3 for XY. Existing
version1/2/3 bytes and asset identities need no migration. An explicit rebuild selects the
profile and preserves the asset ID and old artifacts. Structural validation precedes
native construction, independently of checksums.

Immutable Mesh owns the admitted base. Each Query owns its private derived native
TileCache/NavMesh. Worlds never mutate shared assets or each other's derivatives. Only the
approved pinned Recast/Detour TileCache module is added; a bounded FORGE identity codec
uses standard copying, without another codec dependency or sample framework. Existing
third-party ownership and notices apply.

## Update boundary and failure policy

The finite profile uses 32-cell tiles, at most 64 layers, four layers per coordinate,
eight enabled obstacles, eight touched layers per inflated obstacle, 4096 runtime polygons
and 4 MiB each of payload and derived tile bytes. Preflight calls the same pinned public
query as registration, with room for all 64 admitted layers. The four-layer coordinate cap
avoids its 32-entry internal lookup truncation; the full result must fit eight touched
layers. Footprints span at most 16 tile positions per axis so sparse/empty-coordinate
scans are also bounded. Submit and drain one add/remove request at a time, avoiding the
native 64-request/64-update queue truncation. Complete the snapshot at a writable
navigation owner boundary before exposing queries or moving agents.

Requested queries are warmed at writable synchronization even without an agent. Unchanged
snapshots rebuild no tiles. Failed snapshots retry at most once per boundary, retaining
their exact error for every consumer. Pose changes remove the old shape then add the new
one. Deletion/disable restores the original walkable area. An update error retires the
derivative and fails closed. A successful snapshot changes the local query generation;
agents replan even when their own settings and position match. No persisted native
references, second transform authority or background worker is introduced. Recovery
reconstructs derived state from exact assets and Scene poses.

## Integration requirements

Use ordinary component reflection, validation, Scene/prefab history and persistence.
NavigationTools and automation/MCP share the existing bake owner. Public headers require
matching SDK rebuild/proof. Runtime obstacle edits are not automatically added to the
separate explicit live-tuning contract. Document finite limits and actual platform
evidence; scaffolding and planned workflows are not completion.

The raw-layer producer widens height bounds for emitted portal values and requires
original voxel span plus climb below 255. Admission remains strict; native cache state and
polygon links are constructed rather than deserialized.
