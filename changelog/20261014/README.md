# 2026-10-14

## Particle emitter appearance

- Add per-emitter size and linear RGB factors after shared effect lifetime values; keep motion, clocks and effect resources unchanged by appearance edits.
- Expose saved Inspector values and temporary Live gameplay controls through existing native schemas, history and validated owner-thread commands.
- Backfill neutral defaults for older scenes/prefabs without rewriting sources or changing formats; preserve inheritance and unknown extensions.
- Add original Gallery57 with six appearances sharing one effect and source-free export. The public C++ component layout and exact SDK fingerprint change; rebuild matching gameplay modules. C ABI1 tables remain unchanged.

## Particle shapes and lifetime curves

- Add Point, Box, Sphere and Disc origins, analytic linear drag and bounded size/RGB lifetime stops in the existing Particle Effect document.
- Retain source stop UUIDs through editing/history/copy; strip them from cooked values, admit exact legacy cooks and publish explicit version 2 recipes.
- Reuse private preview, source publication, shared opaque/masked rendering, conservative bounds and source-free export. Add original Gallery56 without changing previous assets.
- Prevent redundant source-update rebuilds when typed dependencies are immutable engine assets; retain exact type/revision checks and failure recovery.
- Validate mathematical, source/runtime/export, actual editor input and native Vulkan paths locally. No native emitter/SDK ABI change; Windows physical acceptance remains pending.

## Layered heightfield terrain

- Author sparse adjoining tiles with additive height layers, masks, resolution
  changes and Raise / lower, Flatten, Smooth and Mask strokes with source history.
- Preview drafts through the isolated shared mesh renderer; publish ordinary Mesh
  assets for existing materials, picking, static collision, navigation and export.
- Add original Gallery55 with camera, light and three spheres landing on real
  terrain. Preserve previous Gallery content and export without terrain sources.
- Check normal/sanitized consumers, actual scaled controls and native Vulkan
  readback. Local source work only; Windows physical acceptance remains pending.

## Reusable mesh particle effects

- Create and publish reusable continuous fountains or bursts with direction, cone,
  speed, acceleration, size and color controls in a Particle Effect document.
- Preview, pause, seek and reset independently of game Play; assign effects through
  Inspector and edit emitter seed/speed from matching C++ modules.
- Reuse the shared mesh renderer and bounded opaque/masked instancing; export cooks
  and dependencies without editable sources or a compiler. Add original Gallery scene 54.
- Validate normal/sanitized runtime, actual scaled editor controls, C++/recovery,
  source-free export and native Vulkan readback. Windows physical acceptance remains pending.

## Overlapping sound effects

- Trigger independent nonlooping effects from an Audio Source through C++, Play controls and the Live sound Inspector, without restarting the main sound.
- Share loaded PCM, follow source/mix/spatial settings, preserve Pause/Resume and retire completed or removed effects within explicit voice limits.
- Extend the existing exact SDK operation; keep ABI1/table layouts and saved formats, with matching C++ rebuilds required.
