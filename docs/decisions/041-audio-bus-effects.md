# ADR041 — Shared linear audio bus processing

Date: 2026-11-07. Accepted; extends ADR036 for optional shared bus effects.
Local CPU/runtime/UI/SDK/MCP/package checks pass. Windows, full editor and
physical output acceptance remain separate.

## Ownership and native graph

Project `game.audio.routing` retains topology, stable keys and authored faders.
Each custom bus may retain an optional `effects` configuration with the same
bounded high-pass, low-pass, echo and reverberation parameters as source effects.
Defaults omit the new block. Existing effects-free routing keeps its scalar path.
AudioRuntime owns the native graph; its existing effect chain implementation is
shared by sources and buses. There is no alternate voice, cursor, mixer or clock.

When any bus has active processing, source output enters its selected native bus
input. The backend sums incoming signals, then applies HPF → LPF → echo → reverb.
A native splitter applies the bus fader and additional send gains after effects.
Outputs connect to downstream custom buses or stock terminal groups; category
faders follow routing and the existing Master applies once. Direct stock routes
use those terminal groups. A continuous native input supplies silence to idle
filters; no callback reads ECS, allocates or services authoring state.

## Pause domains and shared history

Each bus has two bounded native processing domains, selected by a source's
`pause_with_game`. The pausable domain terminates inside the existing pausable
group; the independent domain terminates outside it. Full suspension freezes
both. While running together, linear processing obeys superposition, apart from
floating-point rounding and existing nonfinite protection. Different domain
histories are necessary when one domain's clock is frozen. This decision covers
the existing linear profile only, not future nonlinear processors.

Source Pause/Stop/Restart retains its existing cursor and source-effect meaning.
Shared bus history already produced by several sources continues independently
of an individual source. Rerouting or changing a source's pause policy does not
move or erase earlier shared history. Game pause freezes the pausable bus domain;
independent bus history keeps draining even after its originating sources retire.
Bus faders silence their own retained output while processing keeps advancing.
Downstream bus history can continue after an upstream bus is muted. Master or a
terminal fader controls its full downstream output. Controls retain the existing
native buffered-boundary limitations; they are not sample-accurate cuts.

Cue completion observes Source-owned voices and their own effect retirement.
Shared bus tails are not attributed back to individual events or voice slots.
Each AudioRuntime retains its own bus processing, at most two chains for each of
sixteen buses, with memory derived from those same owners. Candidate and active
worlds retain their existing separate runtime lifetimes.
No tail registry, timer or duplicated stream reader is introduced.

## Authoring and compatibility

Topology and effect settings are Edit-mode project settings; existing session
controls change faders only. Complete candidate validation precedes publication.
Recovery retains configuration and faders, starts fresh DSP history and retains
the exact-SDK host's existing recovery restriction. New retained effect settings
require an additive matching runtime-kit capability. Explicit empty/all-default
blocks also require that parser: earlier strict readers reject the field itself.
Typed defaults omit the block; raw project intent is not silently normalized.
The installed SDK value
boundary and fingerprint are unchanged; bus topology stays project-owned.
Existing source layouts, C ABI1 callback tables/operation values, identities and effects-free
JSON meaning remain unchanged. No dependency revision changes.

## Required proof

Independent PCM must prove combined-source linear processing, fanout without
cursor duplication, live faders/tails, both pause domains, source controls and
rerouting, streams, cue retirement, recovery and resource teardown. Authoring,
automation/MCP, installed SDK, both hosts, kit admission and source-hidden Gallery
coverage belong to the same coherent batch. Windows and physical output require
their own acceptance evidence.
