# Trigger C++ actions during an animation

Timing markers let your C++ code react at a chosen moment in a clip: for example,
a footstep sound, an impact or a change to an object. A marker has a name, a clip
phase and an optional numeric value. Your C++ System decides what it does.

## Try the authored markers

1. Copy the packaged Feature Gallery to a writable folder and open it.
2. Open **Scenes / 11-animation-markers.scene.json**. Walk and Run are real character animations. Idle has no markers.
3. In Content, open **Assets / MarkerPulse.states.json**, then select **Walk**.
4. In **Timing markers**, select a Pulse marker. Phase0.25 means one quarter of the clip; phase0.75 means three quarters. Value1 raises the example character's height; value0 restores it when the optional C++ consumer is built.
5. Use **Add marker**, edit its name/phase/value, or remove it. Press Enter to commit a numeric value. Undo/Redo work on the source; **Save / Publish** makes the changes available to Play/export.

Scrubbing and playing Animation Preview never execute gameplay actions. During
game Play, paused or disabled animation produces no markers; Step crosses one
simulation tick. During a fade, the newly selected animation supplies the markers.

## Connect the included C++ example

The Gallery's **CodeExamples / README.md** walks through registering
MarkerPulseSystem.cpp in the editor's C++ project. Once built, Walk and Run
alternate between normal height and a small stretch at the authored Pulse points.
The timing points illustrate the feature; they are not calibrated footsteps.

A C++ consumer reads copies during the PostPhysics phase:

```cpp
const forge::sdk::Client client(host);
for (uint32_t i = 0; ; ++i) {
    ForgeSdkAnimationEventV1 marker{};
    const auto result = client.animation_event(i, marker);
    if (result == 0) break;
    if (result < 0) continue;
    // Choose an action from marker.name/value and its entity identity.
}
```

Include `<forge/sdk_client.hpp>`, register the System on
`host->post_physics_phase` with `host->fixed_tag`, and allow
`FORGE_SDK_ANIMATION` in the module descriptor. Rebuild using the matching SDK
when you update FORGE. Two Systems can read the same marker.

## Export

Save the scene, choose it as the startup scene, then use **Export Game**.
The published marker data ships with the state set. If you built the C++ example,
its compiled module must be current and the export must use its matching module
kit. The exported game needs no C++ compiler or source files. Test it the same way
as the spinning-cube export.
