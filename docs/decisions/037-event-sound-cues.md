# ADR037 — Bounded event sound cues

## Decision

SoundCue is a Scene/Prefab component that composes existing AudioSource one-shot
requests. Source, Layer, Weighted and Ordered nodes form a bounded acyclic graph
inside that component. Node indices are local structural links. Source leaves use
the existing typed EntityRef and ADR022 portable Prefab member conversion. Prefabs
provide reusable definitions and independent source bindings.

AudioRuntime owns the derived program's transient selection state and expands an
event into its existing generation/clip-bound command queue. AudioSource retains
decoding, loading, streams, voices, spatial settings, effects, routing, pause and
stop ownership. The audio callback receives no graph, Scene, Flecs world or random
state. There is no additional asset registry, sample clock or mixer.

## Admission and lifecycle

The profile bounds32nodes, eight children per branch and16one-shots per event.
Every node participates in shape/cycle/fanout validation, including unselected
branches. Every declared source must resolve in the cue's active loaded Scene
before enqueue; source values and routing validate through their existing owners.
Control-domain requests additionally require all source pause policies to permit
them. Empty/unassigned graphs can be saved as drafts but cannot be triggered.

Layer visits stored children together. Ordered chooses the next child per visit;
it does not schedule a playlist. Weighted uses bounded positive integer weights
and a locally seeded standard generator. A shared node advances per visit. The
pure program returns proposed state without mutating its caller. Failed enqueue
rolls back that event's commands; successful enqueue publishes the new state.
Loading, voice-budget and later stale-source failures retain existing one-shot
semantics. Accepted enqueue does not guarantee audible voice admission.

A cue occupies its own entity without AudioSource. The existing OneShot operation
accepts that entity; other source operations reject it. Source controls remain
responsible for voices, including voices requested by several cues. The ABI1
callback table and operation values remain unchanged. Matching installed component
headers/fingerprint are required for C++ modules.

Observed definition changes and runtime-world retirement reset selection state.
Private recovery reconstructs the saved graph and starts fresh selection, while
the existing Timeline retains its own crossing intent. Audio cursors, effect
memory and already requested one-shots remain outside recovery. No Scene version
or durable identity migration is introduced.

## Authoring and delivery

The Inspector edits a project/Scene/entity-scoped detached draft and submits a
complete property.set_many candidate against the captured Scene revision. Scene
history, native metadata, source/Prefab copying and automation/MCP share the
existing owners. Native discovery exposes the node choices, typed references and
collection bounds. Timeline AudioCue events can target the component through the
same audio service. Source/Play authoring permissions remain unchanged.

Authored and retained partial component values require runtime feature
sound_cues:1 and kit runtime_sound_cues:1. Projects without the component retain
their previous capability requirements. See [Audio](../audio.md) for the precise
format, deterministic selection and runtime limits. Timed/cyclic cue graphs,
shared bus DSP and richer acoustics remain separate work. Native Windows, full
editor and physical-device acceptance are distinct from local source validation.
