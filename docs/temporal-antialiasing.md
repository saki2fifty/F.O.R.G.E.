# Temporal anti-aliasing

`rendering.temporal_anti_aliasing` in Scene rendering version 1 contains `enabled`
(default false) and `stability` (default 0.8, finite 0–0.95). Validation applies even
when disabled, checking the original numeric range before float conversion. The
existing `scene.rendering.set` operation patches nested values, retains omitted
fields and opaque extensions, and owns validation, revisions, Undo, saving and
automation/MCP discovery. There is no additional component, asset or simulation
clock. These product value types are outside the installed native SDK header set;
the C function table and installed SDK fingerprint inputs are unchanged.

## Ownership and geometry

FrameRenderer retains derived history per authored camera. Scene Viewport retains
its separate navigation-camera history through the same TemporalView GPU helper.
MeshSceneRenderer still owns extraction consumers, culling, part selection,
sorting and batching. MeshDrawBundle lazily specializes the same admitted
resources for color plus motion; specialization failures retain ordinary draws.
The existing draw-part capacity also accounts for these additional profiles.

Only depth-writing opaque/masked ordinary Mesh parts accumulate. Static, morph,
skinned and combined morph/skin geometry supply actual previous positions. The
existing 64-member static batching path carries each member's previous transform;
new members have invalid correspondence. Mesh/Sprite instance layouts remain
7/10 rows; the private temporal Mesh variant uses 11 rows. Shadow rendering,
selection, bounds and picking use the unjittered camera. Transparency/transmission,
particles, Sprite/Tile geometry, sky and overlays remain current-frame layers.

Entry poses are immutable snapshots. Equal current/previous values share storage;
distinct snapshots count against the existing pose budget. The last completed
frame advances previous poses only after all camera views succeed. Asset revision
and skin mode must match. Each view also retains its actually submitted part/LOD
keys: a newly visible part or a different LOD cannot claim previous correspondence.
These are disposable presentation records, not authored entity or resource owners.
Two sorted vectors retain their capacity and swap at completion. Each view admits
at most 65536 visible Mesh-part records before target switching; larger views use
ordinary rendering with a warning.

## GPU data and composition

Each view supplies RGBA16F HDR, RGBA32F motion and sampled D32F depth. Motion XY is
current-minus-previous **unjittered NDC**, Z is the actual previous projected depth,
and W states valid surface correspondence. Previous clips use the exact completed
jittered camera and actual previous object/morph/skin values. Invalid/background
pixels have W=0. The eight-phase zero-mean projection jitter spans less than half
a pixel per axis. Camera-relative doubles convert through the existing checked
draw upload boundary.

TemporalFilter owns two RGBA16F histories and one copied previous D32F depth.
Reprojection removes the difference between current/previous raster jitter.
Previous-depth acceptance checks the matching 2×2 raster footprint, linearizing
depth with the supplied projection coefficients; the tolerance is the larger of
0.002 metres and 2 percent of expected positive view depth. Accepted bilinear
history color is clamped to the current 3×3 RGB range before blending. Missing,
out-of-view, depth-inconsistent or invalid correspondence returns current color.
The first/reset frame also returns current color. Alpha always comes from the
current image, including history rejection and masked/custom/background alpha.
Stability zero still uses the
current jittered raster; disabling bypasses AA accumulation and jitter. The shared geometry history can
continue supplying [motion blur](motion-blur.md).

The frame owner first renders the unjittered camera background/sky. A color-only
rectangle copy initializes the private view; motion clears to invalid and depth
to one. Eligible geometry uses its local jittered MRT, then accumulation resolves.
A fullscreen rectangle draw writes the filtered HDR and current depth into the
composed camera rectangle, before remaining layers and bloom/display processing.
No depth subrectangle CopyTexture or source/target sampling alias is required.
Camera rectangles remain isolated, including overlapping independent clears.
The original shared queue order is preserved: if a current-frame opaque layer
precedes a later eligible Mesh draw, the whole view uses ordinary rendering with
a warning. Temporal rendering never reorders Sprite/Tile/particle/other layers to
force an eligible Mesh prefix.

Game cameras must clear both color and depth for accumulation. Other cameras use
the ordinary path with an actionable warning; a warning does not prevent runtime
scene activation. Pipeline/cook/budget incompatibility similarly falls back for
the whole view before switching targets. Geometry errors reject completed history.

## Reset, lifetime and bounds

Scene/provider replacement, catalog epoch/pending transitions, extent, lens,
viewport, orientation/layer changes and rendering-setting changes reset history.
A camera move over 10 metres or direction/up change over 60 degrees is a cut.
Unscaled presentation time rewind or interruption over half a second resets Game
history. Exceptions invalidate derived camera/geometry history without adopting a
partial frame. Scene navigation settles over an eight-frame cycle after a changed
view and then retains its image; preview lighting does not force perpetual resets.

Explicit sequence captures supply an operation/sample-time key from ViewImageTask.
The key owns no time: the existing Timeline remains the sampling authority.
A frozen explicit sample ignores wall-clock interruptions, and automatic exposure
starts immediately from that sample without wall-time adaptation during warmup.
A new sample/operation and leaving capture reset accumulation; capture readiness waits
for eight completed frames before the existing readback/file owner proceeds.
History is never serialized into Scene, capture receipts or runtime packages.

AA-only input targets use 28 texel bytes/pixel; accumulation uses 20, for 48 total.
Optional motion blur adds its documented output/capture payload. Game
admits at most eight accumulated cameras within a shared 512 MiB texture-payload
estimate. Scene View has a separate 512 MiB estimate. Native allocation, driver,
pipeline and retired-resource overhead are additional; this is not a physical
GPU memory or performance guarantee. Admission precedes allocation, typed input
formats/extents are checked, and derived targets retire through existing owners.

## Custom surfaces and validation scope

New surface cooks declare wrapper capability 3 and contain ordinary color, depth
and temporal color roles. Old capabilities 1/2 remain readable. The temporal entry
uses a second owned virtual interface header and the same authored HLSL function
or graph, mask discard, diagnostic and fog behavior. Its strict 208-byte private
object/motion buffer and fixed previous-clip semantic are reflected and validated.
The motion helper participates in shader/material recipe fingerprints. Rebuild an
older custom Shader and its dependent Material to enable temporal rendering.

FORGE's filter and motion implementation are independently authored, using the
approved Diligent resource/pipeline interfaces. The pinned upstream temporal module
is not enabled or copied. Generated-source software Vulkan pixels, independent
motion oracles, actual shared Game/Scene view tests and UI/MCP tests are distinct
from Windows SDK/FXC cooked-surface execution. Windows/full-editor, physical GPU
and graphical exported-game acceptance remain queued; software Vulkan timings do
not establish hardware performance. Gallery 102 exercises original thin geometry
and a shared existing animated foreground object.
The selected software Vulkan stack has an
[open shutdown allocation report](dependency-known-issues.md#native-software-vulkan-shutdown-allocation-report).
Instrumented GPU consumers complete functional checks, then unsuppressed
LeakSanitizer fails with the same 184-byte signature independently reproduced by
a raw native draw without FORGE linked. Their leak checks are not reported as
passes. Selected Scene/UI/codec consumers pass independently; no dependency patch,
upgrade, driver change or sanitizer suppression is included.

## Shared motion-blur mode

The existing TemporalView also supplies [motion blur](motion-blur.md). Geometry
correspondence and AA color accumulation have separate invalidation domains.
A forward explicit sample resets AA but retains the preceding completed pose;
new operations, backward samples and leaving capture invalidate correspondence.
Blur captures preserve their first motion/depth field across AA warmup. No AA
history or jitter is allocated/used in blur-only mode. See the motion-blur contract
for actual mode payloads, raster alignment and Timeline seek ownership.
