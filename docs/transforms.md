# Transform ownership and spatial parenting

## Authored and derived data

Flecs owns three independently inheritable components: `LocalTranslation` (double XYZ), `LocalRotation` (normalized float quaternion XYZW), and `LocalScale` (signed float XYZ). `LocalTransform` is an assembled C++ value, never a registered component. Reading it does not create overrides. Missing rotation/scale mean identity/unit scale; effective translation makes an entity transform-capable.

`WorldTransform` contains a double affine 3×4 matrix, resolution flag and revision. It is instance-owned transient state, registered with `OnInstantiate/DontInherit`. Only the transform evaluator writes it. It is absent from authored JSON, undo snapshots and native reload checkpoints. Prefab prototypes and generated children each receive their own evaluated result. A derived cache is not another authored hierarchy.

The local components use Flecs `OnInstantiate/Inherit`. A move owns only translation; a rotation owns only rotation; a scale owns only scale. Each can independently revert by removing its owned component. `transform.local` requires at least one supplied channel and only writes supplied channels. Copy/paste and reset explicitly author all three. No custom prefab override-mask store exists.

## Structural and spatial hierarchy

Structural `ChildOf` determines organization, recursive deletion and current prefab interiors. A `SpatialBinding` value separately determines transform parent:

| Mode | Effective spatial parent |
| --- | --- |
| `FollowStructure` | Immediate structural parent when it has effective LocalTranslation; otherwise world root. No ancestor search. |
| `World` | No spatial parent, regardless of structural ownership. |
| `Explicit` | Typed EntityRef target resolved within the current scene membership. |

The shared `effective_spatial_parent` function owns this policy. Explicit targets use `(AssetId, EntityId)`, never Flecs handles. Repeated loaded memberships of one scene resolve their own targets. Cross-scene attachment/composition is not implemented: such references remain unresolved, as do missing or non-transform targets. An unresolved entity and its following descendants are not drawn/picked; diagnostics explain the missing binding. Keep-local rebinding can repair it. Preserve-world editing rejects it because no valid world placement is available.

SpatialBinding uses Flecs `Override` for generated prefab interiors; each generated binding is owned. This does not add structured prefab authoring, persistent prefab member IDs or remapping of arbitrary generated-child references.

## Evaluation and invalidation

`WorldContext::evaluate_world_transforms()` gathers live effective Flecs values and feeds one shared `TransformEvaluator`. Iterative parent-before-child traversal detects cycles across structural-follow and explicit edges. World-bound nodes terminate the graph. The evaluator caches local inputs, effective parent and parent result; unchanged values reuse matrix results. Derived writes do not increment authored revisions. A world epoch skips evaluation on unchanged reads; on changes it rebuilds the derived inputs and compares cached values. This is simple correctness-first caching, not a parallel transform scheduler.

The existing wildcard change observer covers set/remove/relationship changes and prefab updates. Native mutable-reference writers must call Flecs `modified<T>()`; they must provide valid normalized rotations and finite scales within[-10000,+10000]. Arbitrary native writes are not transactional authoring commands. The authoring API validates before mutation. Detached transaction/preview adapters use the same graph policy and evaluator without allocating another Flecs world. Rendering, picking and gizmos consume evaluated matrices instead of traversing hierarchy independently.

## Coordinate and numerical contract

- One world unit is one meter; time values remain seconds. Existing numbers are not rescaled.
- +Y is up. Positive rotations follow right-hand axis-angle math. Camera forward at zero yaw/pitch is +Z, with +X right. This camera projection convention is independent of rotation handedness.
- Euler UI inputs are degrees; composition is `Rz * Ry * Rx`, matching legacy scale → X → Y → Z → translation.
- Quaternion storage is XYZW, normalized; q and −q represent the same orientation. Euler display is derived and may show a different equivalent angle triplet near poles. Save never converts through Euler.
- Affine storage is row-major 3×4, multiplying column vectors: `world = parent * local`. Translation occupies indices 3, 7, 11. No third-party matrix ABI is persisted.
- GPU extraction explicitly converts affine columns and direction-equivalent cofactor normal columns to float4 constants (192 bytes). D3D depth and the existing camera projection are unchanged. Picking uses the same affine inverse and primitive meshes.
- Visual local scale permits finite values in[-10000,+10000] on each axis, including zero and float subnormals. Signs are never removed or zero clamped to epsilon. Derived affine matrices retain shear from rotated children beneath nonuniform scales.
- Decomposition rejects normalized-column dot products above1e-6 (shear), not reflection or rank deficiency. Reconstructed linear columns must match within 2e-6 of their column length. Compensation compares translation with 1e-9 absolute + 1e-14 relative tolerance, scale with 2e-6 relative tolerance, and quaternion orientation using normalized absolute dot error below 1e-13.
- Double authored translation is not an origin-rebased renderer. Current camera, meshes, picking and GPU coordinates remain float; large-world visual precision is not claimed.

## Reparent, gestures and deletion

Preserve-world reparenting evaluates `inverse(new_parent_world) * old_world`, validates representable local TRS and writes only channels that need compensation. A translated parent may require only translation; rotated/scaled parents may require all three. Necessary overrides deliberately replace inheritance. Keep-local changes binding/structure without writing any local channel. Reparent defaults to FollowStructure + PreserveWorld; rebinding alone does not change structural ownership.

Move handles request world translation but write only LocalTranslation. R gestures request rotation around a world/view axis at the object's origin but write only LocalRotation. S gestures change local scale only. Conversion rejects unsupported shear or any necessary change outside the requested channel. Invalid previews keep the last valid preview and cannot commit. Confirmation commits one history step; cancel changes nothing.

Deleting a structural subtree removes its owned descendants. A structurally surviving explicit dependent detaches to World while preserving its affine placement if representable. Otherwise deletion is rejected before mutation. Duplication remaps known internal spatial EntityRefs; external refs and opaque plugin payloads remain unchanged. Undo/redo restores authored channels/bindings and reevaluates derived state in the same world.

The existing native ABI world-displacement callback is adapted through parent-aware local translation. It prepares local translation writes from live membership-scoped Flecs entities, including generated prefab children without authored rows. All results validate before writes. Simultaneously moving parent and child moves the child once; generated handles survive. It does not expose arbitrary transform registration or change channel ownership. The callback now runs on the runtime fixed tick.

## Future ownership

Future physics and animation must provide ordered local pose/input updates through an explicit ownership phase. They must not compete with the transform evaluator by writing WorldTransform. Fixed ticks and derived local-pose interpolation are described in [runtime timing](runtime-timing.md). The physics and Ozz modules now provide their ordered simulation/pose consumers; general origin rebasing remains deferred.

See [scene-v3](scene-format.md), [authoring API](authoring-api.md), and the [user guide](../manual/editor/transforms.md). Flecs policies were checked against the pinned [ComponentTraits source](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/docs/ComponentTraits.md).

## Signed scale, inverses and singularity (Phase7)

Forward parent×local evaluation accepts singular transforms. This does not make
an inverse available. Inversion first scales the linear3×3 by its largest
coefficient, computes adjugate/determinant and reciprocal infinity-norm condition
number, and requires `rcond >= 1e-8`. This scale-invariant check rejects extreme
axis imbalance as well as angular degeneracy. The rounding budget
`64 * epsilon(double) / 1e-8 < 2e-6` leaves room below the TRS recomposition
tolerance; it is not a guarantee for arbitrary input error or large translation
cancellation. Nonfinite inverse results reject the operation too. Uniform tiny
scales can be well-conditioned; one tiny axis beside unit axes need not be.

World editing resolves the actual effective parent; it never recovers a parent
by inverting the child. Keep-local parenting and forward evaluation work beneath
a singular parent. Preserve-world parenting or a world edit requiring that
parent's inverse fails without changing authored state/history. Picking uses
double local-ray math; singular/ill-conditioned geometry has no local picking hit
and remains selectable from Hierarchy. Bounds use forward-transformed vertices.

Decomposition enumerates proper rotation bases, preferring existing scale signs
and then quaternion proximity with the previous hemisphere. Without a previous
value, unit positive scale/identity rotation is the deterministic preference.
Rank-two missing axes use an oriented cross product; rank-one completion projects
the previous basis to preserve twist; rank-zero retains the previous rotation.
Every result must recompose within the column-relative tolerance. No pseudo-inverse,
authored matrix, or combined ECS transform is added. Compensation writes only
channels that actually change; explicit scale edits retain equal-value intent.

Normal directions use cofactor(A), multiplied by sign(det(A)) for a reliable
nonzero determinant, then scaled by one common positive magnitude. This agrees
with inverse-transpose direction for invertible matrices without dividing by a
small determinant. At rank two it supplies the surviving oriented surface-area
normal; rank-one/zero or collapsed normal directions produce zero, never a made-up
normal. A determinant within16double epsilons of the sum of its six absolute
products is treated as numerically indeterminate/singular for parity, avoiding
frame-to-frame sign flips on composed rank-two matrices. This derived numerical
classification never invalidates authored LocalScale.

Preview rasterization uses opposite front-face state for reflected solid draws;
planar primitives and singular surfaces are explicitly two-sided. Old procedural
triangles are oriented to their outward normals before upload. No descendants
are removed as a zero-scale optimization. Full imported tangent/normal mapping,
skinning and WARP capture acceptance remain Phase7 integration gates; primitive
preview math alone does not establish those capabilities.

Physics has independent admission: pinned Jolt supports sign-insensitive centered
boxes, spheres and capsules. Only for these symmetric shapes, magnitudes are baked
into dimensions while authored signed scale remains unchanged. Sphere/capsule
magnitudes must be uniform; zero/too-small Jolt scale, unrepresentable shear and
FORGE dimension/ancestry failures reject physics realization. See[physics](physics.md).

## Numerical compatibility

Positive-only scene3/4 and prefab1 documents remain readable and retain their
versions. When a known owned LocalScale needs the expanded domain, scene output
is promoted to5 or prefab output to2, using existing version rejection. Old
editors reject those versions before rewriting data. No component identity or
field layout changes. Once promoted, ordinary saves do not downgrade; Undo can
restore the original earlier document. Unknown payloads are not traversed.
Negative zero is canonicalized only in known scale values on output. The exact
SDK fingerprint includes `visual-scale=signed-zero-v1`; old modules must rebuild
and pass matching installed-consumer/Play tests before being called compatible.
