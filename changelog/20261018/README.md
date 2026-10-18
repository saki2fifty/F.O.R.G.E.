# 2026-10-18

## Windows delivery — Build261018-000192

Build **261018-000192**, compiled source
`d8b33f67f3918a61cbf3baac4d68a2cc1525c7cf`, adds:

- Sequence sound-cue points using existing Audio Sources, with silent seek/preview.
- Editable Gallery70 demonstrating coordinated sound, motion and camera cuts.
- Scene/Game **Save image…** commands preserving rendered PNG colors and alpha;
  Game images include runtime UI.
- Seven real editor screenshots and the technology stack in the repository README.

The [complete release gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39166092978)
passed all nine jobs: core 147/147 and native SDK 180/180 on each platform,
editor 267/267, shared-game relocation, formatting and final package acceptance.
The final ZIP passed the 24-stage SDK flow, 306-step C++ onboarding and 31-step
fresh-process shortcut check. Its 1536 manifest files, 72 x64 executables/libraries
and 462 Gallery files/70 scenes match their hashes and immutable source. The exact
SDK ships in the same organized ZIP; gameplay modules need the matching rebuild.

Native Windows image-export checks preserve all 246 project source files. Scene
PNGs match the previously reviewed outputs byte-for-byte at 100/150/200%; the new
Game PNGs were also reviewed. Normal source preparation finished before commands
ran, without disabling import or changing asset identities. Physical Windows,
native folder/file pickers, GPU smoothness and listening acceptance remain deferred.

The earlier [full source audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39158203128)
measured the same production code: responsive-window medians were about 368/388ms
for empty/cube projects, with 70.4/73.7MiB working sets, versus 307/315ms and
62.3/61.4MiB for the older rebuilt baseline. These are retained costs and host
variance, not game FPS, first-GPU-frame timing or a performance-threshold pass.
Build191 failed fixture checks and was never delivered. This ZIP retains its
compiled-source documentation; these delivery notes do not change its bytes.

## Windows delivery — Build261018-000190

Build **261018-000190**, compiled source
`84553c800d3a6dbf636532a9fad99d840b3dc1f3`, delivers:

- Sequence camera shots and editable Gallery68, including stopped Game preview.
- Personal editor shortcuts with conflict feedback, persistence and reset.
- Reviewed runtime tuning: choose changed fields after Stop and apply one Scene Undo step.
- Scene color filter, saturation and contrast, with editable Gallery69.
- Separate, optional Graphics GPU durations in the existing Profiler.

The [complete Windows/Linux gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39095070659)
passed all nine jobs: core145/145 and native SDK178/178 on each platform,
Windows editor263/263, shared-game relocation, formatting and final package acceptance.
The extracted package passed the 24-stage SDK flow, 306-step C++ onboarding and
31-step fresh-process shortcut flow. Two source/toolchain-free relocated games
executed the edited reverse RotationSystem with the reviewed Speed360.

The ZIP includes the exact Native SDK, runtime kits, manual and69 editable Gallery
scenes. All1535 manifest files, 72 x64 executables/libraries and461 Gallery files
were verified against their checksums and immutable source. Actual D3D12 WARP
captures were reviewed. The ZIP retains documentation from its compiled source;
these delivery notes do not alter or relabel its bytes.

Physical GPU, input feel, audio and desktop acceptance remain deferred. WARP
startup measurements for unchanged production source1465a51 completed in the
[source audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39084122081):
responsive-window startup was approximately394–396ms versus256–262ms for the older
accepted baseline, with72–75MiB working set versus61MiB. These are retained costs
across the wider feature baseline, not game FPS, first-frame timing or evidence
that GPU profiling improved performance. The measurement job has no regression
threshold. Representative hardware/smoothness testing remains outstanding.

Build190 excluded sequence sound cues and rendered-view PNG export. Build192
above delivers them after their native and final-package gates.
