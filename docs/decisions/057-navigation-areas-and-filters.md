# ADR057 — Navigation classes and query-time routing policy

Status: accepted for implementation; extends ADR026/027/031/032. Not a shipped
capability until its integrated acceptance gate passes.

## Ownership and authored data

Add optional NavigationArea source classification through ordinary reflected
Scene/Prefab data. Area0 is the existing default; classes0–15 fit the pinned native
polygon's sixteen filter bits. Classification applies to included walkable geometry
and explicit links on that entity. It does not include geometry by itself, create
physics layers, infer materials or turn obstacle masks into walkable surfaces.
Existing source inclusion, spatial/static ancestry, resource and profile ownership
remain authoritative. There is no area registry or second geometry database.

Add optional NavigationAreaFilter to an Agent: a sixteen-bit allowed-area mask
stored in uint32 and sixteen fixed float costs indexed by area. Defaults allow all
areas with cost1. Costs are finite1–1,000,000; this retains the pinned default
distance heuristic's lower bound. Mask0 admits no routing surface. Costs multiply
route distance, not movement speed. The fixed array is reflected as bounded data;
no dynamic cost-container or separate routing manager is introduced.

Classification belongs to baked geometry. Costs and filtering belong to each query
and Agent, so changing preferences does not rebake or duplicate a NavMesh. Query
uses call-lifetime native filter values; one Agent must never mutate a shared
Query's filter for another consumer. Default existing queries retain their meaning.
Read-only projection, paths, constrained movement, avoidance and retained-link
validation must use the applicable policy through the existing query owner.

Policy edits replan at the next legal navigation boundary. An admitted explicit
crossing completes with its retained policy before replanning. Disable/clear and
physical gravity keep their existing ownership; a filter is not a physical wall.
Scene/Prefab/history, live tuning and Keep review share the existing property
operations and validators. Static classification remains build-only.

## Native encoding and compatibility

Default source, settings, geometry digests and positive envelopes1–8 stay canonical
when classification is unused. Add an explicit classified envelope9 when selected
source/link classes are nondefault. Preserve logical AssetIds and immutable older
artifacts. An old worker must not silently publish an unclassified result for a
classified request. Validate the candidate's expected profile before publication.

The native rasterizer reserves0 for null and63 for generic walkability, and merges
compatible overlapping spans using the larger area. The classified profile maps
logical0–15 to raster1–16, then back to logical polygon areas. Higher classified
area wins an eligible overlap; default63 must not erase a custom class. Static
baking and dynamic tile callbacks share this conversion. Legacy layer encodings
remain unchanged. New polygon flags are exactly1 shifted by logical area; class,
flags, links, raw layers and structural bounds are checked before native use.
Do not weaken legacy admission to accept arbitrary native flags or area bytes.

Existing per-Query TileCache ownership and obstacle failure policy stay in force.
Rebuilds preserve classification; routing policies do not mutate shared assets.
No dependency upgrade, copied external implementation, general geometry rebake or
new pose authority is involved.

Private navigation recovery extends only for configured/retained routing policy
that earlier profiles cannot preserve. Retain older canonical profiles when unused,
mixed ordinary/physical crossings and physics-owned poses. Reject empty, malformed,
conflicting and downgraded extensions before candidate publication. SDK Play keeps
authored restart; no arbitrary custom-state migration is implied.

## SDK, automation and acceptance

Expose typed values through the matching installed SDK and append a sized POD
filtered-query callback under the existing Navigation provider/permission/thread/
tick rules. Preserve the existing callback prefix and default-query behavior.
The exact private fingerprint changes; modules must rebuild with their host. This
does not establish a stable cross-version C++ ABI or add another provider.

Editor and automation/MCP use normal source/property/history and navigation APIs;
update discovery, schemas, refusals and actual Play coverage in this batch. Include
real cheaper-route selection and exclusion, stale/rebuild/cancellation, both planes,
dynamic carving, links/physical/behavior consumers, fixed-array Meta/SDK boundaries,
fresh recovery/negative payloads, editable source-hidden exported Gallery examples,
authentic captures and current-use manual instructions. No capability completion
claim from class metadata, a filter toggle or SDK scaffolding alone.
