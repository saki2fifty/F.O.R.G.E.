# Independent music and UI audio

[unpaused_audio.cpp](unpaused_audio.cpp) is an original exact-SDK module example
for the editor package's Feature Gallery scene84. Its music and UI sources have
Pause with game disabled; the ordinary gameplay speaker keeps the defaulttrue.
The source uses public SDK headers and the same shared Flecs as its host.

Configure and build against a matching installed SDK from its required compiler
environment:

```text
cmake -S <sdk-root>/sdk/sample -B AudioBuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DFORGE_NATIVE_SDK=<sdk-root>
cmake --build AudioBuild --target unpaused_audio_gameplay
```

The module ID is `project.unpaused_audio`; its declared dependencies are
`forge.audio` and `forge.input`. Use the existing exact-SDK project declaration
and matching fingerprint. This file supplies a complete module entry point;
an existing module can reuse its control callback through that module's entry.
It is not an extra entry point to paste beside another generated entry point.

Use [unpaused_audio.input.json](unpaused_audio.input.json) as the project's input
map. Its control-phase actions stay available during game Pause. In SDK Play,
focus the Game view and use R for a UI one-shot, T to stop its effects, and Y to
verify that a gameplay-only sound rejects a control-domain request. The example
uses Gallery84's persistent scene/entity references; substitute your own references
when integrating it into another scene.

The existing `sdk::Client` queries the Audio capability, which is callable in fixed
and control callbacks. Fixed calls keep their normal queued behavior. Control
calls can target only active sources whose Pause with game policy is disabled;
stale references, ordinary sources and suspended/preparing output reject. No
native audio object, ECS mutation command or separate clock is exposed.

Stop, replacement and faults silence old output. This supports bounded PCM clips,
not streaming music. A module must be rebuilt whenever the installed SDK
fingerprint changes. Windows/physical playback and graphical export are separate
acceptance checks from compiling this example or running its offline tests.
