# ADR051 — Runtime UI in stopped sequence presentation

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
This profile concerns Scene sequences; a single clip's Edit Scene overlay remains
outside this initial profile. Arbitrary DOM property tracks and native CSS seeking
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

Sequence presentation is noninteractive: no OS clipboard/text/IME, surface input,
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
