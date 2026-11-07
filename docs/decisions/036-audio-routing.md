# ADR036 — Project-owned scalar audio routing

Date:2026-11-07. Accepted; the bounded scalar profile is implemented locally.
Windows, physical output and delivery acceptance remain separate.

## Existing ownership and decision

AudioRuntime already owns native voices, Master/pausable groups, source effects,
clip/stream leases and the final per-voice category gain. Project game settings own
the four initial volumes; existing live/standalone controls own session overrides.
Extend those owners for bounded custom buses and post-fader sends. No second
mixer, native voice registry, sample clock, asset kind or dependency is needed.

The initial profile is scalar routing without bus DSP or delay. An immutable
derived projection from each custom bus onto Master/Music/Effects/Dialogue is
mathematically equivalent to summing its paths in that profile. Apply that scalar
at the same existing source/effect output, retaining one sample cursor and the
current pause, streaming, one-shot, tail and spatialization policies. Native audio
callbacks continue to read only backend-owned state.

## Authored contract

Optional `game.audio.routing` is an object with integer `version:1` and `buses`.
At most16 buses have unique stable project-local `key`, optional display `label`,
`volume`0..1, one `output` and up to3 additional `sends` (`target`, `gain`0..1).
Keys are1..64 lowercase ASCII letters/digits/underscores, starting with a letter;
the four terminal keys master/music/effects/dialogue are reserved. Labels are
optional single-line UTF-8, at most128 bytes, and are never binding identities.
Targets are exact terminal/custom keys. Duplicate targets, missing targets and
cycles reject, including zero-gain edges and muted nodes. Input collection order
does not change routing identity or the deterministic projection.

The primary output has weight1. Sends carry additional post-custom-fader signal.
Products of bus faders and edge gains sum along paths; stock terminal faders
follow them and the existing Master group applies once to the complete signal.
Preflight each node's potential path sum with all custom faders at1; it must be
at most16. This bounds future live fader changes without silently normalizing
sends or clamping authored intent. It is not a general-purpose bus-effect graph.

AudioSource appends default-empty `custom_bus`. Empty retains the exact existing
bus0..3 meaning; a nonempty key selects a defined custom bus while preserving the
stored stock choice. Unresolved keys report an explicit source admission failure,
never an alias by label/index or a fallback route. Existing Scene/Prefab/history,
typed codecs and value validation own this field. The matching gameplay SDK must
be rebuilt; C ABI1 callback records/tables remain unchanged.

## Settings, control and compatibility boundaries

The optional graph remains project-owned game settings. Per-user/live bus faders
are key-scoped numeric overrides; they cannot change topology. Existing settings,
host mixer commands and source controls must admit complete candidates before
touching output. Legacy four-fader requests retain current custom values. A live
custom-fader request must match the admitted key set. Ordinary settings/source
history and persistence remain separate; no cross-document rename is implied.

Old game defaults without routing keep their representation. Older Scene/Prefab
values receive default-empty custom_bus through the existing codec/backfill;
identities and earlier fields retain their meaning and behavior.
Exported custom routing requires an explicit matching runtime-kit capability,
including retained custom source selections and applicable Prefab overrides.
Private recovery must validate the admitted graph and preserve the existing host
mix state; ordinary audio recovery still makes no sample-cursor continuity claim.
Determine the exact existing host wrapper extension from its implementation,
rather than inventing another recovery manager or parallel settings database.

Ordinary Play uses root checkpoint3 when the admitted project has custom buses.
Its audio block1 records the exact project graph document and complete host mix.
Admit that definition and every fader against the current project before source
preparation/publication; failed recovery leaves the active world and host mix
untouched. This private exact-configuration check can reject reordered project
documents without changing source key identity or scalar graph semantics. Projects
without custom buses retain root2; root2 cannot restore a required custom profile.
The exact-SDK Play host retains its existing explicit recovery restriction. No
sound cursor, queued one-shot, effect delay or arbitrary native memory is saved.

Runtime-kit profile `runtime_audio_routing:1` is required both by nonempty project
topology and authored retained source keys, including partial Prefab overrides.
An empty optional graph or default-empty source selection retains legacy kit
admission. The matching SDK fingerprint owns the appended native source layout.

## Completion requirements

Independent graph/path-sum tests and actual offline PCM must prove routing,
amplification bounds, live volume/source changes, pause, effects/tails, one-shots,
streams, loading and invalid-candidate behavior where affected. Native controls,
source/Prefab/history, programmatic automation/MCP, matching installed SDK,
recovery and source-hidden original Gallery/export coverage belong to the same
coherent batch. Reconcile current user guides and authentic captures before a
completion claim. Bus DSP, convolution and broader acoustics remain unimplemented.
