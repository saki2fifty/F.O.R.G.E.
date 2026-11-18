# 2026-11-18

## SDK module header dependencies

The built-in prefab, Character2D, physics-ray/work and Tile Map modules declare
the pinned JSON target required by typed SDK value headers. Clean module builds
receive their includes independently of unrelated engine targets. The SDK value
boundary, dependency revision and user project format are unchanged.

## SDK snapshot metadata delivery

SDK Play can reference schema metadata that the editor already holds, using a
content digest, encoded size and active-world identity. Legacy and explicit
schema reads retain complete metadata. Inspector, automation and MCP consume the
same resolved schema; scene replacement and shutdown retire its derived identity.
Candidate admission and response validation retain the full inline size budget,
and pipe limits and request deadlines are unchanged. Opt-in runtime diagnostics
distinguish response construction from completed pipe writes. The internal SDK
control probe now retains one pending request and releases completed receipts,
preventing its paused control loop from filling the bounded request queue.

## Input Message source inspection

Console Gameplay input expands each named message source to show its action,
payload, source send count and last sent tick/encoded payload preview. The preview
is captured before observers mutate native source storage, retained across idle
ticks and limited to2048UTF-8bytes with encoded size and explicit truncation.
Route changes, deactivation, source replacement and new runtime worlds reset
source diagnostics. Native callback delivery retains its existing full-value copy.

Play automation and MCP return the same read-only source metadata. Observations
without inspection fields show unavailable inspection explicitly.
The illustrated manual and existing message Gallery instructions explain the
readout. No authored InputMessage layout, Scene/Prefab version, C callback table
or dependency change is required.
The private SDK fingerprint includes message implementation semantics; rebuild
project modules against the matching kit even though public value layouts stay unchanged.

## Avoidance membership, masks and yielding priority

Navigation Avoidance supports independent32bit membership and observed-peer
masks, plus finite0..1 priority. Equal or higher priority peers influence steering;
selection is asymmetric. Default values retain previous mutual steering, while
surface boundaries and physical collision retain their existing owners.

The same reflected policy supports Edit/history/Prefab, live Inspector controls,
selective Keep review, existing component/Play automation and MCP, and the matching
typed SDK. Private recovery14 retains nondefault policy and mixed area filters.
Default source/recovery representations remain unchanged. Project modules require
the matching SDK fingerprint. Native optional-field snapshots preserve runtime
Prefab changes and resets to omitted defaults.

Editable Gallery233/234 repeat priority and asymmetric high-bit mask examples in
3D and XY, with one NavMesh and the existing patrol graph per Scene. Both run from
relocated source-hidden content exports. The manual explains the controls and
includes an authentic native Inspector capture. Physical Windows review remains
pending.

## Navigation areas and independent routing costs

Eligible navigation sources and authored links support sixteen logical areas.
Each agent can permit a subset and assign independent traversal costs while
sharing one baked NavMesh. Static and dynamic-obstacle profiles support both3D
and XY. Policy edits replan at the next legal boundary; an active link crossing
finishes under its admitted policy. Character pose ownership stays with physics.

Inspector exposes allowed-area masks and sixteen labeled costs, including live
editing and selective Keep review. Scene overlays color the admitted classes.
Existing component/history/Play automation and MCP access use the same owners.
The matching SDK adds typed filtered queries and an appended sized C callback;
rebuild project modules against the matching SDK fingerprint.

Classified assets use navigation envelope9 and classified dynamic layers. Older
default-area assets remain readable with their existing canonical representation.
Private navigation13 recovery preserves configured and retained policies.
Gallery231/232 demonstrate direct, expensive-area and excluded-area routes with
editable Cameras, shared behavior and ready navigation in both planes.

## Delivery acceptance coverage

Navigation MCP checks use the current movement range, camera inspection waits for
the queued Pause acknowledgement, and the destination Inspector test releases its
initial focus before scrolling and clicking the actual control. Existing source,
history, clipping, activation and observation assertions remain in place.

The installed navigation header documents physical jumps and descending drops for
both grounded profiles. Its exact SDK fingerprint changes with the header bytes;
component layouts and the callback table remain unchanged. Rebuild project modules
with the matching SDK.

## Physical character descents

3D and Grounded XY navigation can follow explicit descending zero-lift links
between static supports. Physics owns walk-off, gravity, collision and landing;
navigation supplies bounded horizontal steering. Actual blockage, missed landings,
cold realization, manual handoff and stopping distance retain the same owners.
Private navigation12 recovery preserves mixed jumps/drops without another pose
database. Authored fields, cooked navigation and installed SDK layouts are unchanged.

Gallery229/230 include editable 3D/XY descents, Cameras, native walls and ready
NavMeshes. Existing automation/MCP controls and matching C++ SDK clients exercise
the same workflows. The manual describes setup, controls, saving and export.
General platformer reachability graphs and moving-platform routing remain pending.

The full CPU benchmark test retains all 28 native workload cases with a bounded
300-second aggregate deadline. This accommodates Windows process startup while
preserving workload, numeric, report-integrity and graceful-exit checks.

## Physical XY character jump links

Grounded Character Controller 2D navigation can traverse explicit positive-lift
XY links between static physical supports. Native physics owns takeoff acceptance,
gravity, collision and descending landing. Navigation supplies bounded horizontal
steering and reports actual progress; a missed or obstructed landing cannot count
as arrival. Manual movement, zero-time synchronization and capsule replacement
retain the existing command priority and one-step lifetime.

Private physics14/navigation11 recovery preserves mixed physical3D/XY jump intent
without a second pose owner. Downgraded, malformed and empty extensions reject.
Editor controls, source history, automation/MCP and matching installed C++ SDK
consumers use the same implementation. The grounded route diagnostic identifies
an unsupported corner by index and XY coordinates.

Gallery228 includes two editable navigation islands, bidirectional positive-lift
links, static launch floors, raised landings and a physical obstruction. Source
and relocated export share the same behavior. The manual describes setup,
controls, saving and export. General platformer drop graphs and moving-platform
route planning remain unavailable. Physical Windows/GPU review remains separate.
