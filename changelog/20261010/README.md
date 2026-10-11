# 2026-10-10

## Transform animation timeline

- Content can create and open reusable `.animation.json` clips in a central Animation Timeline.
- Author local position, rotation and scale keys; add/update/delete keys, drag their times, choose interpolation and use clip Undo/Redo.
- Scrub, play/pause, loop and change preview speed without changing the authored Scene or physics.
- Save & Publish clips, attach them through Transform Animator, and play them using the existing runtime animation phase.
- Standalone content includes validated cooked clips and requires no source files or editor.
- Feature Gallery adds `08-transform-animation`, with a moving, turning and resizing platform. Windows packaging includes its cooked clip and manual.

No numbered Windows delivery has been created for this batch. Windows CI and physical acceptance remain pending. Bone editing, IK, blending, event tracks and reverse playback are outside this bundle.
