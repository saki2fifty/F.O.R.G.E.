# Surface motion blur

Scene rendering version 1 accepts `motion_blur` with `enabled` (default false),
`shutter` (default 0.5, finite 0–1) and `max_pixels` (default 32, finite 0–32).
Validation applies even while disabled and checks JSON numbers before narrowing.
The existing `scene.rendering.set` operation owns partial nested edits, opaque
extensions, revisions, Undo, persistence and automation/MCP discovery. Scene
lighting controls call that operation and lock authored edits during Play.

## Rendering

The original MotionBlur pixel operator owns a pipeline, binding and uniform only.
The existing TemporalView owns its input/output textures, completed camera/parts,
optional AA filter and capture snapshots. MeshSceneRenderer supplies real object,
camera, morph, skin and instance correspondence through the existing surface-motion
variant. No additional animation clock, pose database or rendering manager is used.

Supported opaque/masked Mesh draws must form the existing sorted queue prefix and
test/write depth. Game cameras must clear color and depth. Missing compatible
surface shaders, incomplete geometry, resource changes or excessive payload do not
publish partial history. Unsupported views preserve ordinary draw order and report
an actionable diagnostic. Existing custom surface capability 3 supplies motion;
older readable capabilities retain their ordinary fallback.

The order is Mesh MRT, optional AA, motion blur, camera HDR/depth composition,
optional spatial focus, current-frame layers, bloom and display. Sprites, Tile
Maps, transparency, particles, sky and overlays remain current-frame. Alpha always
comes from the current center. No sampling/render-target feedback is allowed.

Unjittered current-minus-previous NDC motion converts to view pixels, multiplied by
shutter and clamped to max_pixels. A bounded trailing exposure gathers the current
image along positive velocity: an earlier translated image at pixel p is the
current image at p+velocity*t. At most 33 trapezoid-weighted positions use bilinear
color samples. Clear depth, missing correspondence, invalid numbers, less than
half-pixel travel, unrelated depth and inconsistent neighbour movement retain the
current sample or exclude that neighbour. Depth tolerance is max(0.002 m, 2% of
positive camera depth); velocity tolerance is max(1 px, 25% of center speed).
Perspective, orthographic and infinite-far forward projections share the existing
positive-depth validation owner. This is bounded surface detail exposure, not
physical shutter integration, foreground dilation or hidden-surface reconstruction.

## History, capture and memory

Normal blur works without AA: no raster jitter or AA filter/history is created.
Scene navigation renders one additional stationary image before retaining it, so
a stopped camera does not freeze its last blurred image. Completed geometry is
adopted only after a successful complete frame.

Timeline remains the sampling, seek, lease and restoration owner. Frame rendering
borrows ViewImageTask's operation/time key. A forward sample in one operation resets
AA accumulation while retaining preceding completed geometry. Its first actual
motion/current-depth field is copied into disposable view targets and retained
through all warmup renders. The snapshot-to-current raster offset compensates for
AA jitter; current depth must still match the captured surface before blur samples
are admitted. Repeated renders do not replace real sample motion with stationary
warmup motion. The first export sample has no prior correspondence and stays sharp.

New operations, backward samples, leaving capture, failures, camera discontinuities,
settings, extent or resource changes invalidate applicable history. The Timeline's
existing preview generation also signals manual seeks/context replacement and loop
rewinds to the shared frame owner. Continuous forward playback and explicit held
sample warmup preserve that generation. Main routes preview transitions to the
same frame invalidation API; it does not sample poses itself.

Current texture payloads per pixel are: 28 for HDR/motion/depth inputs, plus 20
for AA history, 8 for blur output, and 20 for a frozen motion/depth pair. Thus blur
alone costs 36, combined AA/blur 56 and combined frozen AA/blur 76 bytes per pixel.
Up to eight Game views share a 512 MiB current payload estimate; Scene has its own
512 MiB estimate. Extent/mode growth stages required textures before adoption;
disabled modes release unused derived targets. Native allocation, retired/staged
resources, other passes and pipeline overhead are additional. These estimates do
not establish physical GPU memory usage or performance.

## Validation boundaries

Local numeric pixel probes and actual shared Scene/Game consumers are distinct
from full Windows Main/MSVC/FXC, cooked D3D12, physical GPU/input/DPI and relocated
graphical EXE acceptance. Those platform checks remain queued. The selected native
stack's [open shutdown allocation report](dependency-known-issues.md#native-software-vulkan-shutdown-allocation-report)
remains unsuppressed; functional rendering is not a clean native LSan result.
