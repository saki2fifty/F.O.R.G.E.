# 2026-10-21

## 2D physics development milestone

- Added distinct XY Physics Body 2D and box, circle and capsule colliders through
  the existing physics, transform, authoring and fixed-step owners.
- Added typed C++ movement, force/impulse, angular control, state, filtered rays,
  overlaps and current/retired contact observations. Matching modules rebuild
  against the updated exact SDK; vendor handles remain private.
- Integrated bounded private candidate recovery, saved property history, Prefab
  intent, animation/navigation ownership checks and named Inspector controls.
- Added Scene collision outlines and original editable Gallery75, with falling
  Sprite shapes, a moving Kinematic platform and a sensor. Included optional
  C++ helpers, prepared assets and a user experiment.
- Added focused CPU/process/installed-SDK/source-free content checks, high-speed
  shape/sensor cases, native render captures and shared-game export/relocation
  gates. Windows execution and physical acceptance are reported separately.

Build261021-000195 predates this milestone. No 2D joints, character controller,
navigation or generated Tile Map collision is included. See the
[user guide](../../manual/editor/physics-2d.md) and
[technical contract](../../docs/physics-2d.md).
