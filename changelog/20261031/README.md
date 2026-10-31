# 2026-10-31

## Custom polygon one-way boundaries

Static/Kinematic nonsensor Tile Maps now support one-way convex polygons,
concave outlines, holes and separate regions. Original front-facing edges supply
their own slope/landing planes. Decomposition diagonals and hole bridges cannot
become platforms. Rigid contacts, fast-body CCD and character physical queries
share admission through the existing physics owner. Compound visitors use the
actual contacting piece for side evidence.

Cell/source replacement, last-good geometry, independent instances, Reset and
fresh recovery retain their existing owners. Original-boundary acceleration is
body-owned and rebuilt from checked native geometry after recovery. Source IDs,
source/cooked formats, C table layout and private codec10 remain unchanged.
The new custom one-way configuration carries its derived edge masks; older valid
configurations keep their exact representation. Rebuild C++ modules against the
matching SDK contract fingerprint.

Gallery125 demonstrates roof, hole-floor and island landings plus upward passage
to an ordinary ceiling. It adds one Scene and preserves the prior 335 asset records,
124 Scenes and project settings. Existing Inspector, automation/MCP, C++ and export
workflows use the same geometry. The README and manual share an authentic Game
capture. Dynamic/sensor one-way collision remains unsupported. Windows and
physical desktop/GPU acceptance remain pending.

## Dynamic Tile Map bodies

World-bound Dynamic Physics Body 2D now admits a complete Tile Map collision layer
as one compound body, including reflected polygons, holes, islands and sensors.
Area-density mass uses a weighted physical center and combined centroid inertia;
positive mass overrides scale inertia proportionally. Existing impulses, torques,
velocity, locks and recovery use the same physics owner. Dynamic one-way response
remains unsupported.

Checked cell/source replacement retains the body-origin pose and center-of-mass
and angular velocities. Invalid candidates retain last-good geometry; edited
instances keep their adopted source until explicit Reset. Empty geometry retires
the body and refill starts with zero velocity. Source IDs, formats, public C table
and checkpoint layout remain unchanged.

Gallery124 compares independently falling/tumbling, mirrored 12 kg and
rotation-locked instances, with ordinary source-free export. Inspector/discovery,
existing automation/MCP operations, C++ consumers and the manual cover the
workflow. The manual and README reuse an authentic native Game capture.
Windows and physical desktop/GPU acceptance remain pending.

## Tile neighbor rules

Tile definitions can select existing local definitions with up to 64 ordered
eight-neighbor conditions. Same/Different follow cell orientation and layer;
first match wins and fallback uses the painted definition. Selected images,
animation and collision share one immutable derived value. Saved cells, SDK
queries and checkpoints retain logical paint IDs. Compact overrides reuse
unchanged chunks and neighbor halos, with bounded renderer/world admission.

Source/cooked/recipe5 retain older 1..4 reads and guard reserved legacy extensions.
Inspector compass controls, target selection, priority and Apply/Discard share
document history and busy rules with automation/MCP
`forge_asset_document_tile_autotile`. Gallery122 demonstrates edges, a hole,
a flipped L, an isolated tile and animated interiors. The manual includes real
Inspector and Game captures. Matching modules must rebuild with the current SDK;
the public native C table and outer recovery envelope retain their contracts.
Windows editor and physical hardware review remain pending.

## Animated tile sequences

Tile definitions support looping atlas frames with individual durations. Static
fallback frames, cell orientation and fixed collision retain their existing owners.
One world-scoped completed-tick sample follows Pause, zero rate, Step and restart;
the source tab has a separate local preview. Renderer values update in place only
when a displayed frame changes; all sequence frames share existing mip banks and
resource limits. No per-phase cache entries or per-cell animation entities are added.

Source/cooked/recipe4 retain older1/2/3 reads and guard opaque legacy animation
fields before promotion. The private Map recovery block is2; copied observation
manifests are2, with legacy untimed observation reads. Timing stays outside cell
transfer identity. Matching native modules require rebuilding with the current
exact SDK fingerprint. The outer recovery envelope and public native C table
retain their existing contracts.

Inspector, automation and MCP share sequence validation, document revisions,
history, permissions and publication. Gallery121 reuses original FORGE arrow
artwork and adds three editable assets, preserving322prior records and120Scenes.
Windows/MSVC/full-editor/physical-GPU/graphical-EXE acceptance remains pending.

Thirty selected FORGE units pass strict ASan/UBSan compilation across fourteen
consumer links. Twelve CPU/UI/MCP/runtime consumers exit cleanly with leak
detection enabled. Both native consumers finish functionally and retain the exact
known unresolved184-byte/five-allocation shutdown condition; no suppression is
used. Four normal/strict/authored/source-free relocated Game display pairs match
exactly. The matching installed SDK, all121GalleryScenes, offline manual and
protected-source checks pass. No numbered package or remote action accompanies
this local source.

## Concave tile collision

Tile Set collision adds Polygon(decomposed): one simple counterclockwise outline
with3–8 unit-cell vertices. A bounded deterministic triangle decomposition preserves
inward L/U notches. Existing strict Convex admission and merged FullCell rectangles
remain unchanged. Pieces share the existing Static/Kinematic native body, sensor,
query, controller, last-good publication and4096shape budget. Holes/multiple contours,
Dynamic Maps and custom-polygon one-way response remain unsupported.

Source3, cooked3 and recipe3 retain1/2reads, source identity/allocator/history and
legacy opaque-field guards. Copied Play definitions carry nested version3 through
the existing transfer/receiver. Public SDK layouts and checkpoint fields stay
unchanged; changed value-support code updates the existing exact SDK fingerprint,
so matching native modules must rebuild.

Inspector and the existing automation/MCP collision operation use the same source
owner, validation, revisions, permissions, history and publication. Gallery120 adds
an original L/U atlas and five assets, preserving all317prior catalog records and
119prior Scene sources. Native upper/notch landing, relocated source-free playback,
four exact Game display pairs and an installed-only relocated SDK consumer pass.
Authentic Inspector/Game captures accompany the usage guide. Windows/full-editor,
physical input/GPU and graphical exported-EXE acceptance remain pending; no numbered
package accompanies this local source.

Twenty-eight selected strict units and13consumer links pass. Twelve CPU/UI/MCP /
runtime consumers exit cleanly with unsuppressed ASan/UBSan; the native Gallery
finishes functionally and retains the exact known unresolved184-byte shutdown
allocation condition. Normal and strict Gallery displays are identical. Offline
manual/package fixtures and protected-source checks pass.

## Moving one-way platforms

Kinematic nonsensor Boxes and full-cell Tile Maps can use One-way collision.
Solid-side checks pair actor and platform poses from the same fixed boundary;
native contact, controller movement and optional platform transport retain their
existing owners. Transform clips and queued C++ targets drive the platforms.
Dynamic, sensor and custom-polygon one-way response remain unsupported. Public
layouts and checkpoint fields are unchanged; use the matching SDK fingerprint.

Gallery119 supplies an editable moving course and inline ledge, with landing and
passage, relocated native playback and four exact source-free Game display pairs.
Authentic Inspector/Game captures accompany the usage guide. Normal native/source /
UI/MCP and relocated installed-SDK checks pass, including432translation cases and
32actually rotating rigid cases. Seventeen selected strict units and11links pass;
ten CPU/UI/MCP/runtime consumers exit cleanly with unsuppressed sanitizers. The
native renderer finishes functionally with the exact known unresolved184-byte
shutdown allocation condition. Offline manual and package-fixture checks pass.
Windows, physical GPU/input and full-editor/exported-EXE acceptance remain pending.
No new numbered package accompanies these local changes.

## Local diffuse probe lighting

Reflection Probe maps can supply local base diffuse lighting with a separate,
default-zero **Diffuse intensity**. The existing map resource, transformed volume,
priority and world-distance fade serve both lighting modes. Lit Meshes and Mesh
particles receive the light through native material layering; direct illumination,
emission and other global specular terms retain their existing behavior. Eight
active assigned probes total remain supported, with complete-set and last-usable
resource fallback. Shader 13 retains 19 roles and readable versions 1–12. Matching
SDK modules must rebuild for the appended public value.

The shared Inspector, live controls, authoring/history and MCP expose the scalar.
Gallery 118 has two warm/cool overlapping volumes and source-free relocated export
proof. Authentic controls and renderer captures accompany the usage guide. Local
normal functional checks pass, including all 19 graph roles and the actual combined
shader binding profile. Thirty-six selected strict units and ten consumer links
pass. Eight CPU/UI/MCP/runtime sanitizer consumers exit cleanly; two native
functional consumers reproduce the exact known unsuppressed 184-byte shutdown
allocation condition. Offline manual and package-fixture checks pass.
Sampled irradiance grids, runtime recapture and multi-bounce lighting remain
unavailable. Windows, physical GPU and full editor acceptance remain pending.
No new numbered package accompanies these local source changes.

## Light images

Directional/Spot 2D and Point cubemap Textures modulate direct RGB illumination through
the existing Light, Texture, resource and draw owners. Source sampler/filter/mip
intent is retained; alpha is ignored. Light image width/height/tiling have shared
Inspector/live/automation/MCP history and save behavior. Public Light layout changes
require a matching SDK module rebuild. Shader 12 keeps 19 roles and 1–11 readers, with
shared Decal/image slots and explicit complete-set fallback.

Gallery 117 compares repeating arrows, a patterned Spot and a captured-Cube Point
light using existing Textures. Its Scene/three-Texture source-free export has four
exact matching displays. All314 prior catalog records are unchanged. Authentic
Inspector and renderer images accompany the task-based manual. Local functional
checks cover PBR, projection/mips, resource recovery, UI, MCP and relocated SDK.
Thirty selected strict units and nine consumer links pass. Seven CPU/UI/MCP/runtime
sanitizer consumers exit cleanly; two native consumers finish functionality and
reproduce the known unsuppressed 184-byte shutdown allocation condition. Offline
manual and package-fixture checks pass. Windows, physical GPU and full editor
acceptance remain pending. No new numbered package is created.

## Many shared material projectors

A visual scene supports 128 active Decal Projectors. Larger receiver lists use
conservative spatial selection with ordered storage buffers while preserving the
existing 0–8 unique map/sampler budget. Small lists keep a constant packet. Bounds,
Camera/layer filtering, complete resource admission, priority/EntityId order and
instance batching stay with their existing owners. Outside or unavailable grid
domains evaluate the complete list; capacity failures retain ordinary materials.
Private Shader capability 11 retains 19 roles and readable versions 1–10.

Gallery 116 adds 64 tinted floor projectors sharing one badge Material and Texture,
with no C++ module or new import. Its source-free relocated export produces four
exactly equal display readbacks. All 313 prior catalog records remain unchanged.
The manual includes an authentic renderer capture and placement/limit guidance.
Automation/MCP uses the same recipe, properties, atomic history and document saves.

Focused CPU and software-Vulkan functional checks pass, including 128 projectors,
affine/large-origin/dense/fallback cases, normal/roughness resources, instance batching
and frame transitions. These results do not establish Windows/D3D12, physical GPU,
full graphical editor or exported-EXE acceptance; those reviews remain pending.
No new numbered package or remote delivery accompanies this local source work.
