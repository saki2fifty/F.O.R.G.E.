# ADR051 — Runtime UI in stopped animation presentation

Status: accepted and implemented. Native CPU, scoped sanitizer and software Vulkan
render/export checks pass; physical desktop/GPU acceptance is separately pending.

## Ownership and scope

Extend ADR021's Scene-owned UI value preview limit for stopped Scene sequences.
AnimationSequenceEditor retains its existing copied Runtime Context, Scene,
controller selection, resource preparation, sample/seek and export guards.
UiRuntime in that Context projects the committed sampled values through its
existing document/model/state/localization and protocol admission. The UI model
uses tick0 and paused=true. Scene/controller identity and the existing preview
generation scope the snapshot. No gameplay tick, model registry, animation clock,
Scene/Prefab write or public SDK/Scene-format change is introduced.

The Game preview and frame exporter consume this same sampled snapshot through
SequenceFrameSource. Sources without UI retain their prior rendering behavior.
Single-clip Game preview also uses this noninteractive presentation profile. Its
Timeline samples the existing complete transform/component/material proposal;
AnimationUiPreview projects that same proposal through a copied Runtime Context,
Scene and UiRuntime. Scene::reset owns atomic replacement and retained entity
handles. It retains source Prefab definitions and copied authored types without
running animation, gameplay, physics or audio. Project, Scene revision, target,
clip and document generation scope the copy; restarting advances its presentation
generation. Pending material preparation withholds the UI candidate. No second
sampler, clock or UI model registry is introduced.

The existing per-entity single-clip Edit Scene overlay remains unchanged. Stopped
UI composition and frame export are available in Game for both source kinds.
Single-clip export borrows the existing Timeline sampler and copied UI Context
through SequenceFrameSource. The current target, Scene/source revisions, project
generation, catalog and export token guard every explicit sample. Normal preview
playback is held; the prior cursor/preview/play state is restored only while its
source remains current. A clean published clip and prepared materials are required.
Existing PNG/WebP/silent AVI output, cancellation and output authorization remain
owned by ViewImageTask. New clip receipts identify clip/target explicitly; existing
sequence receipt formats remain unchanged. Unrestricted Edit Scene HUD composition
remains outside this profile. Arbitrary DOM property tracks and native CSS seeking
remain unsupported. Preview CSS presentation uses a fixed zero time rather than
pretending to seek transient CSS state. Keep simultaneous binding/transition
writers on one property outside the supported value-animation workflow.

## Presentation and lifetime

Reuse RuntimeUiHost, UiPresenter and UiDiligentRenderer. Make the host purpose
explicit for Runtime, Source and Sequence presentation, preserving ADR048's single
presenter/backend lifetime owner. Preview-purpose switches retain borrowed image
views through existing ImGui/backend consumption. Stage/Play/project/close resets
retire previews through this owner; there is no second RML initialization owner.
Resolve competing authoring previews deliberately, without reallocating two
purposes repeatedly in one frame or silently editing either source draft.

Stopped animation presentation is noninteractive: no OS clipboard/text/IME, surface input,
gameplay command delivery, live field authority or runtime asset publication.
Internal native events may update presentation, but queued gameplay commands are
discarded through the presenter's current queue owner. Existing Runtime and
source-preview input/secret/source guards remain intact.

Native update, resource readiness, layering, supported RenderTexture outputs and
screen composition use the existing UI and frame-rendering paths. A failed or
pending UI candidate must not publish a partially composed current frame. Errors
and generation mismatches remain explicit. Frame export retains cancellation,
source-current checks, end-exclusive sampling and cursor restoration; offline UI
uses explicit density1. Editor preview retains its normal viewport density.

## Acceptance boundary

Prove copied nested/repeat samples and native RML width, opacity and class results,
then actual displayed/exported pixels and supported output textures. Cover source
preview/sequence/Play/project handoffs, old image lifetime, resource failure/retry,
noninteractive authority and Scene/history isolation. Source-hidden relocated
export, existing automation/MCP image/frame workflows, authentic manual captures
and scoped normal/strict/sanitizer/graphics gates belong to the batch. Physical
Windows/GPU/input acceptance remains separately attributable.

For single clips, Timeline and automation/MCP share seek/play/stop ownership and
the same complete sampled proposal. Reads identify projected UI models separately
from displayed pixels. Document/session/revision, source-busy and existing grant
checks apply. Preview never authors Scene/source values or history; Play and
competing source-preview handoffs retire stopped presentation authority.
