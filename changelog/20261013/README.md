# 2026-10-13

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
