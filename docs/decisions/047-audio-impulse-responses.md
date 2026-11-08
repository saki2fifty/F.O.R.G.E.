# ADR047 — Project-owned audio impulse responses

Date: 2026-11-08. Accepted; extends ADR036/041 with bounded linear convolution.

## Existing ownership

An optional bus `effects.impulse_response` contains a typed AudioClip identity and
wet mix. Project Settings remains the configuration owner. Native controls borrow
the project's Content catalog; automation/MCP uses the same draft and candidate
validation. The AudioClip pool owns decoded data and leases. AudioRuntime owns
bus graph preparation, callback processors and teardown. No secondary asset type,
resource cache, mixer, voice clock, authoring database or tail registry is added.

Required audio waits for complete graph preparation before source voices/output.
Unsupported or missing data fails with bus/asset context rather than silently
playing dry sound. Active responses are resident48000Hz mono/stereo, at most
96000frames, with per-channel absolute coefficient sum at most64. This is a
bounded initial profile; normalization, resampling, cross-channel true-stereo and
physical acoustic simulation are outside it.

## Processing and lifetime

The original implementation uses a256-sample direct head and512-point FFT tail,
with257retained positive-frequency bins and conjugate reconstruction. Shared
immutable per-asset kernels and separate mutable pause-domain histories preserve
ADR041's linear superposition and independent/game-paused clocks. The impulse
follows the ordinary bus chain, before faders and sends. Shared tails outlive an
individual Source Stop; Source cue completion does not acquire bus history.

All kernel/history/scratch allocation precedes callback attachment. A32MiB retained
processing budget includes kernels and both domain histories, but excludes decoded
PCM and existing native/filter overhead. Exact finite-tail accounting permits an
idle-domain fast path without truncating retained samples. Native teardown waits
for callbacks before state release. Candidate and active worlds retain their
existing separate AudioRuntime lifetimes and budgets.

## Persistence and compatibility

Absent configuration preserves legacy JSON. Wet0 bypasses loading/processing while
retaining the reference and parser capability requirement. Configuration/recovery
retains exact intent and faders, rebuilding fresh DSP history. No processing buffers
or source cursors are persisted. The existing typed asset-impact adapter and full
export root collection explicitly include project response clips. No UUID scanning
or duplicated authoritative dependency graph is introduced.

Kits need additive `runtime_audio_bus_impulse:1` for any retained assignment,
including wet0. Unsupported kits reject before publication and preserve a previous
good distribution. AudioSource/native component layouts, C ABI1 callback tables,
operation values, asset identities and pinned dependencies retain their contracts.
Matching installed SDK consumers use the same implementation. Project bus values
are not a native component-layout or callback-table change.

## Required proof

Independent direct PCM verifies dense partition boundaries, stereo, wet mixes,
complete maximum-length tails, chunking/in-place processing and silent reactivation.
Native tests cover shared/fanout buses, pause/mute, Source Stop, invalid clips and
aggregate budget. Both hosts, recovery, installed SDK, Settings/MCP, real native
controls, typed asset impact and source-hidden full export are required. The
configured sibling AudioImpulseResponses sample preserves Gallery project settings.
Synthetic inventory and Linux CPU tests do not imply Windows executable, full
editor, physical listening, device deadline or external-client acceptance.
