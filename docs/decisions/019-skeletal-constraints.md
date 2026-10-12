# ADR019 — Skeletal constraint ownership

Date:2026-10-12. Local implementation; native Windows and physical acceptance are
separate gates. This extends F39 without closing retargeting/general rig graphs.

AnimationRuntime owns skeletal evaluation and existing fixed clocks. Optional
TwoBoneIK and AimIK components reside on the imported model holder. Durable
ModelNodeAsset references bind to its exact admitted skeleton revision; display
names, array positions, transient entity handles and another model do not bind.
Reach requires a direct start-middle-end chain. One reach and one aim evaluate in
that order after clip/state/additive mixing and root-motion neutralization.
The exact pinned Ozz native jobs supply corrections, before the existing complete
pose/transform/physics transaction. There is no independent rig manager or clock.

Targets and pole directions are values in skeleton model space. This bounded
profile has no target-entity dependency edges and therefore no constraint graph
cycles. Ordinary C++ fixed systems can update values before Animation. The holder
transform supplies placement in the world; no implicit inverse of the holder is
required. Local axes are normalized in the adapter. Active IK requires stable
nonzero directions/planes, nonzero chain lengths and available joint inverses.
Visual signed/zero scale admission remains unchanged. An unavailable inverse is a
constraint failure, never a silent identity solution. Active joint bases require
orthogonal axes and uniform scale magnitudes, including signed uniform scale.
Signed scale conjugates the correction into local rotation space. Nonuniform
scale/shear would require a nonrotational local correction and therefore rejects
only the constraint operation; it does not restrict ordinary visual transforms.

Clip-intent channels retain their existing ownership. Constrained rotation paths
join that union: a path without a clip curve starts from the admitted rig rest
rotation. Other unanimated channels use the current ECS locals, including authored
translation/scale and ancestor rotation. Effective joint parenting must match the
admitted model hierarchy. Native corrected rotations flow into normal ECS channels,
renderer skinning and bounds; no competing pose database is introduced. Removal,
disable or retarget resets former rotation paths from the sampled base once and
then releases them. Stop retains normal authored-scene restoration.

Bind/cache only derived resource indices; validate the entire candidate before
adopting inputs or writing transforms. The extracted root-motion joint cannot also
receive an IK correction. This rejects double rotation ownership while allowing
constraints on ordinary descendant joints. Failed corrections cannot partially
apply a chain or move its holder.

Interrupted state fades freeze the pre-constraint mixed local expression. Reapply
constraints once after each new blend; freezing an already corrected partial pose
would accumulate correction. Recovery uses existing exact resource/clock/source
validation and reconstructs constraints from typed scene data; no new authored
asset format or persistent constraint binding cache. Numeric target/weight/twist/
softening changes retain phase, reuse bindings and interpolate at presentation.
If an intermediate native constraint sample is unavailable, presentation metadata
falls back to the admitted current fixed sample; existing transform presentation
retains its fixed-pose interpolation. Presentation does not mutate simulation.
Binding/axis/pole and unanimated base-transform changes snap the successful pose. Actors without constraints keep
their existing single LocalToModel pass; enabled constraints add native model passes
for merged ECS base values and each correction.

SDK value headers/fingerprint include the new components. ABI1 remains unchanged;
native modules require the matching rebuilt SDK. Solver pointers stay private.
