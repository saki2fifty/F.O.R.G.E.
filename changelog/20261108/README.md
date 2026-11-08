# 2026-11-08

## Ordered sound playlist authoring

Select up to eight AudioClips in Content, review their order and playback settings,
then create a finite or looping cue with child Sources as one Undo step. Ordered
graph children can move up/down in the detached Inspector draft before Apply.
The existing Sources, SoundCue graph, Scene history and portable Prefab references
remain authoritative; no playlist asset, runtime component or new scheduler is
introduced. Automation and MCP discover the same compound authoring operation.

The illustrated manual covers creation, later editing, startup, looping, pause,
saving, reusable Prefabs and export. Gallery198 contains a reusable three-tone
Prefab instance. Its source and relocated cooked playback are checked against
independently ordered ordinary Source requests. Windows/physical review and a
new numbered delivery remain separate.

Audio discovery now includes inherited Sources, listeners and cues. Persistent
read-only native queries stay uncached and release during module shutdown before
native World cleanup. Component layouts, SDK fingerprint, C ABI and serialized
formats remain unchanged. Use a runtime built from the matching current source
to include the inherited-audio correction.
