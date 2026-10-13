# 2026-10-13

## One-dimensional animation blend spaces

- Content adds reusable clip samples on a numeric axis, with model-scoped clip
  selection, cycle duration, source history, publication and atomic Scene attachment.
- Animator selects neighboring clips with Blend position at a shared cycle phase;
  positions clamp at endpoints. Existing morph, root-motion, IK and physics consumers
  retain their complete-pose ownership. Preview changes remain temporary until applied.
- Exact recovery and source-free export include cooked space/model dependencies.
  Gallery43 compares three positions and includes an ordinary C++ control example.
- Matching SDK modules need rebuilding after the public Animator layout change.
  ABI1 remains; native Windows and physical acceptance remain pending.

## Reusable animation bone masks

- Content adds an Animation Mask document with skeleton-scoped bone selection,
  fractional influence, descendant inheritance, source Undo/Redo and Save/Publish.
- Animator's Bone mask combines with Blend subtree for Blend/Additive animation,
  root motion and morph curves. Runtime binding and replacement share existing
  resource, clock and complete-pose transaction owners.
- Recovery validates exact mask revisions; export ships cooked masks and their
  typed dependency closure. Gallery42 compares whole, subtree and weighted masks.
- The Animator header changes the matching SDK fingerprint; C++ modules need a
  rebuild. ABI1 remains. Native Windows and physical acceptance remain pending.

## Rig target viewport controls

- Reach and aim targets have labeled Scene markers and model-axis handles,
  available from their Inspector, Scene View menu and command palette.
- Dragging uses parented model space, including mirrored/nonuniform placement.
  A temporary draft commits one Undo step on release; cancellation preserves the
  scene. Prefab targets retain ordinary property override and Undo behavior.
- Live markers observe the running scene in Forge's Scene/Game views; the tinted
  Inspector owns runtime tuning. Standalone games contain no editor tools.
- Unrelated normal entities no longer advertise an empty Live Inspector.
- Native Windows captures and physical acceptance remain pending for this local
  source bundle. Local source/UI evidence is tracked separately from delivery.
