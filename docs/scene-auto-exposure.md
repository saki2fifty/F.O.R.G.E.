# Automatic Scene exposure

Scene rendering version1 accepts optional `auto_exposure`. Omission disables it.
Finite settings are: `enabled` false; `min_ev` -8 and `max_ev` 8, both within
[-20,20] with min≤max; normalized `low_percentile` .02 and `high_percentile` .98,
within [0,1] with low<high; `target_gray` .18 within [.001,1]; `speed_up` 1 and
`speed_down` 3 within [.01,20]. Rates are exponential inverse unscaled seconds.
JSON bounds/order are checked before float conversion, then the typed candidate
is validated again. Disabled settings still validate. Unknown fields survive.
Older consumers preserve this optional section as unknown data but do not execute
automatic gain; rendering version1 and existing Scene identities stay unchanged.

The existing `scene.rendering.set` operation merges the section, validates the
complete Scene candidate and owns one atomic Undo/Redo. Human controls, automation
and the discovered `forge_scene_rendering_set` MCP tool call that same operation.
MCP uses normalized percentiles; the editor displays percentages. Scene Save,
reopen, live render extraction and ordinary runtime packaging retain the values.
There is no separate exposure asset, Prefab field or authored database. Prefabs
can contain lights/cameras but do not own Scene display settings. This internal
Scene settings header is outside the installed native SDK whitelist: this change
does not add a gameplay SDK API, layout or function-table/fingerprint revision.

## Meter and display order

FrameRenderer meters the composed Game RGBA16F image once, after ordered camera
geometry and view-local bloom. Scene View and explicit material/model previews
retain their personal/manual exposure. The independent FORGE histogram/reduction
uses public pinned Diligent compute/pipeline/buffer interfaces, not another
engine's implementation or an upstream temporal post-effect.

The meter takes a deterministic center grid of at most256×256 pixels. A16×8 tile
uses128 local histogram bins over log2 luminance[-16,16], then writes its own
histogram. A second dispatch reduces at most512 tiles and clips each bin's sample
weight to the chosen percentile interval, including fractional boundary weights.
Rec.709 luminance uses positive RGB channels with weights[.2126,.7152,.0722]. Only
finite RGBA pixels with positive finite luminance count; empty black/invalid pixels
do not invent light information. Quarter-stop bin centers introduce at most .125
stop quantization inside the logarithmic profile; outside values clamp to its ends.
This is a bounded sampled meter, not an exact full-resolution statistic or mask.

The target is `clamp(log2(target_gray) - clipped_log_mean, min_ev, max_ev)`.
For a valid continuing history, gain follows
`target + (clamped_previous-target)*exp(-rate*delta)`. Increasing gain uses
speed_up; decreasing gain uses speed_down. An empty image uses neutral zero gain
clamped to the limits, marks history invalid and lets the next valid image snap.
The initial valid image and reset images also snap directly to their target.

DisplayResolve multiplies HDR light by `2^(manual_exposure + automatic_gain)`,
then applies the existing enabled linear color filter, native PBR Neutral mapper,
saturation/contrast and single sRGB transfer. Alpha, invalid-color diagnostics,
camera composition, editor overlays and later runtime UI keep their existing
contracts. Manual compensation and color grading do not feed back into metering.
This remains SDR display output; HDR-monitor output is a separate capability.

## Derived history and admission

The host supplies its existing unscaled presentation timestamp: SDL ticks for
standalone and ImGui time for editor Game. The meter creates no clock and changes
no simulation timing. Elapsed time is clamped to .25 second; equal timestamps
do not advance. Initial/rewound time, enable, world/renderer/resource replacement,
Scene identity, target size, active camera identity/order/count, viewport and
projection changes reset history. Pending resources and their first ready frame
also reset. This is not a camera-teleport detector or motion/depth history.
CPU bookkeeping commits only after a successful resolve.

The fixed retained buffer payload is262224 bytes:262144 histogram,16 state and64
uniform. This excludes driver alignment, pipelines, descriptors, candidates and
in-flight references; it is not total VRAM. Disabled paths compile/allocate no
automatic resources. Previously created resources may stay cached; the HDR source
binding is cleared when disabled. Sources require a valid single-sample RGBA16F
or RGBA32F 2D shader view and bounded uint32 pixel addressing/device dimensions.
The existing presentation owner admits compute support and compiles/caches shaders.
Optional Windows device requests retain manual rendering on devices without it;
enabling the effect requires actual compute support. Invalid settings/time/source
reject before metering; optional resources are staged before adoption. Failures
use the existing presentation diagnostics and retain the last accepted display
where its owner has one. There is no product CPU readback, synchronous GPU wait,
general authored-compute feature or parallel resource manager.

## Evidence and remaining acceptance

Scene/history/reference checks, actual ImGui input at three scales/two window
sizes, MCP schema/replay/revisions/grants/save, native software-Vulkan histogram,
percentile/adaptation equations, composition/reset/last-good/manual-grading checks
cover this workflow. Gallery96 uses an original held light-intensity animation and
ordinary shared Mesh/Material assets. Source-free relocated content and actual
Game rendering are checked separately. See the screenshot record for capture
provenance. Software rendering does not establish physical performance, Windows
editor/D3D12 execution or physical acceptance; those remain explicit review gates.
