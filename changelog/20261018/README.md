# 2026-10-18

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
The extracted package passed the24-stage SDK flow,306-step C++ onboarding and
31-step fresh-process shortcut flow. Two source/toolchain-free relocated games
executed the edited reverse RotationSystem with the reviewed Speed360.

The ZIP includes the exact Native SDK, runtime kits, manual and69 editable Gallery
scenes. All1535 manifest files,72 x64 executables/libraries and461 Gallery files
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

Sequence sound cues and rendered-view PNG export have local implementations and
focused validation; their native Windows and delivery gates are still pending.
They are absent from Build190.

## Local implementation — Sequence preview frames

The ready sequence Timeline can export a bounded range of its silent Game preview
to numbered PNGs in a new folder. Start/FPS/count settings, current dimensions,
progress, Cancel and a final frame receipt use the existing image task. The
previous preview cursor and playing state return afterward; source changes or
resizing stop the operation. No gameplay, sound, movie encoder or SDK change is
part of this editor operation.

Focused Linux software-Vulkan checks pass for exact sample pixels/order, actual
Gallery 70 animation/camera images, source preservation, cancellation, failures,
shutdown and real settings controls at 100/150/200% in two window sizes. Normal
and sanitizer owner/lifecycle checks pass. Native Windows compilation, full editor
visual review and a delivered package for this addition remain pending. It is
absent from Build190 and the separately frozen Build192 candidate.
