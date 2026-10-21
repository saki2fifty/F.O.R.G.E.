# Optional C++ behaviors

## 2D bodies and sensors

Scene75 runs its saved bodies and moving platform without an optional module.
`Physics2D.hpp` provides small helpers for code in a matching SDK project. In a
registered fixed system **before Physics**, obtain the actor's canonical
`ForgeSdkEntityRefV1` with `host->entity_reference`, then call:

```cpp
forge_gallery_launch_2d(host, actor, 2.0, 5.0); // XY metres per second
forge::sdk::Client(host).set_angular_velocity_2d(
    actor.scene_uuid, actor.entity_uuid, 1.0); // radians per second about +Z
```

Call these when a gameplay action occurs; calling them every tick continually
resets the body's velocity. Each call queues its own command. The module must
request Physics permission, and the body must be an admitted enabled Dynamic.
The changes affect runtime state, not the saved Scene.

In a registered **PostPhysics** system, inspect the current tick's copied events:

```cpp
forge::sdk::Client client(host);
for (int32_t i = 0, count = client.contact_count_2d(); i < count; ++i) {
    ForgeSdkPhysicsContact2DV1 event{};
    if (forge_gallery_sensor_event_2d(host, unsigned(i), event)) {
        // event.begin distinguishes entering from leaving.
        // first is the sensor; second is the visitor.
        // Check availability and epoch before acting on a current body.
    }
}
```

Events are broadcast observations: reading them does not consume them. They
expire at the next admitted synchronization. A retired body's End can have the
same authored UUID as a replacement; its epoch distinguishes that realization.
No vendor physics handles or headers are exposed. The installed SDK's
**physics_2d_gameplay** sample compiles these helpers and exercises the real host;
it is a validation probe, not a behavior to attach unchanged to Scene75.

## Sequence, camera-shot and sound-cue speed

Scene68 runs its saved shots without a C++ module. From an ordinary owner-thread
C++ system before Animation, include `SequenceControl.hpp` and call:

```cpp
forge_set_sequence_speed(controller, .5f);
```

The controller must have Animation Sequence. Speeds0–4 are accepted; zero holds
time. Object clips and camera shots keep sharing that controller's clock. This
does not edit authored files or Camera enabled flags. Use the matching installed
SDK. The existing animation service controls pause, seek and restart on that same
controller; bound clip clocks remain controlled by the sequence.

Gallery70's Audio Cue points use that same sequence time. Slower playback delays
future cue crossings; speed zero or Pause holds them. Already requested effects
keep playing under their Audio Source controls. Seeking does not play skipped
sounds; Restart rearms the zero-time cue for the next advancing tick. Native C++
can inspect `forge::AudioCue` from `<forge/animation_components.hpp>` using the
matching SDK. The cue stores its sequence/source EntityRefs and time in seconds;
the Audio Source still owns the clip, gain, pitch and routing.

## Material animation

Scene67 plays its saved clips without a C++ module. To adjust an animated object's
speed from an ordinary C++ system, include `MaterialAnimation.hpp` and call:

```cpp
forge_set_material_animation_speed(actor, .5f);
```

This uses the existing Transform Animator, accepts speeds0–4 and rejects a bound
sequence track because its controller owns speed. The animation owner samples the
saved material tracks into that object's Mesh Renderer. Shared material assets
remain unchanged. Use the current matching SDK; compiled modules from an older
fingerprint must rebuild.

The installed SDK sample target **material_animation_gameplay** also demonstrates
pause, seek, resume and restart through the existing animation service, then reads
the resulting values in PostAnimation. It is a validation example; the Gallery
does not activate it automatically. Export normally includes the saved cooked
clips. If you add your own C++ module, export includes its compiled module too.

## Object material values

Scene66 runs from authored values without a gameplay module. Include
`ObjectMaterialValues.hpp` from a C++ system and call, for example:

```cpp
forge_set_object_material_value(actor, "surface", "roughnessFactor",
                                {forge::MaterialParameterType::Scalar, {.25f}});
forge_revert_object_material_value(actor, "surface", "roughnessFactor");
```

Use the logical slot displayed by Inspector. Helpers set bounded typed component
intent; the renderer checks actual material declarations and retains the last good
draw on rejection. Shared materials and source files stay unchanged. Build with
the matching SDK after this Mesh Renderer layout update. The installed SDK sample
target **material_value_gameplay** exercises changes at fixed ticks, including
color changes and Revert. The Gallery scene does not activate that probe by default.

## Two-axis animation blending

Scene65's original column instances already run from their authored Animator
settings. `SetBlendPosition.hpp` also provides
`forge_set_blend_position(actor, x, y)` for a C++ system to set both coordinates
together without restarting the admitted cycle. The one-coordinate overload
remains available. Both reject invalid values before changing the component.
Build your module with the editor's matching SDK after the Animator layout update.

## Runtime Prefab creation

Use a writable copy of the Gallery with the matching FORGE editor/SDK.

1. In **Build**, create the C++ project if needed and test compiler tools.
2. Copy `SpawnPrefabs.cpp` to `Native/Systems/SpawnPrefabs.cpp`.
3. In `Native/CMakeLists.txt`, after `add_library(gameplay ...)`, add `target_sources(gameplay PRIVATE Systems/SpawnPrefabs.cpp)`.
4. In `Native/gameplay.cpp`, before the anonymous namespace, declare `void forge_register_system_SpawnPrefabs(const ForgeSdkWorldV1* host);`. In `start`, after `forge_register_systems(host)`, call `forge_register_system_SpawnPrefabs(host);`.
5. Save and build the C++ project. RuntimeEntities is intrinsic; do not add its bit to descriptor permission fields. Keep the template's Diagnostics permission to report failures.
6. Open `64-runtime-prefab-creation.scene.json`, save and press Play. Two robots appear on the next creation boundary and rotate in opposite directions. A third appears after two seconds and rotates faster. Stop discards all three runtime-created groups.
7. Save this Scene as startup in Project Settings. Its Runtime Dependencies already include `RuntimeRobot.prefab.json`. Export with the matching runtime/module kits and run the result. The game carries compiled C++ and the Prefab; the player needs no compiler.

The example uses this Gallery's saved Scene/Prefab UUIDs. Adapt those constants
for your own project, and declare your selected Prefab on a reachable saved Scene.
It requests only three groups, releases each observation after Ready and writes
the root through the fixed system's Flecs stage. Children follow their existing
structural spatial policy. New World-space physics children would need explicit
placement. No source file, editor history or saved scene is modified during Play.

Scene11 plays without C++. To see its Pulse markers change the characters' height,
register MarkerPulseSystem.cpp in a FORGE-created C++ project. Work on a writable
copy of the Gallery and use the editor/SDK from the same build.

1. In **Build**, create the C++ project if it does not exist and test compiler tools.
2. Copy this example to `Native/Systems/MarkerPulse.cpp`.
3. In `Native/CMakeLists.txt`, after `add_library(gameplay ...)`, add:

   ```cmake
   target_sources(gameplay PRIVATE Systems/MarkerPulse.cpp)
   ```

4. In `Native/gameplay.cpp`, before the anonymous namespace, declare:

   ```cpp
   void forge_register_system_MarkerPulse(const ForgeSdkWorldV1* host);
   ```

   In its `start` function, after `forge_register_systems(host)`, call:

   ```cpp
   forge_register_system_MarkerPulse(host);
   ```

5. In that file's descriptor, set `allowed_capabilities` to
   `FORGE_SDK_ANIMATION` (OR it with existing permissions). This is the permission
   field immediately after `required_capabilities`; consult
   `forge/native_sdk.h` for the named descriptor fields.
6. Save and build the C++ project. Open `11-animation-markers.scene.json`, save it
   and press Play. Walk and Run stretch vertically by12% at phase.25 and return
   to normal at.75. Idle is unchanged. No extra component needs attaching.
7. Stop, choose this saved startup scene in Project Settings, save settings and
   export with the matching runtime/module kits. The compiled action travels with
   the exported game. A compiler is only needed on the development machine.

This example assumes the scene's authored character-root scale1. It uses copied
marker observations and ordinary Flecs LocalScale writes in PostPhysics. Pulse
values1/0 are illustrative clip timing, not calibrated footsteps or automatic
sound playback. The example source is compiled by the native marker integration
fixture, which checks its actual effect through runtime presentation.

## Optional one-shot return to Idle

Copy `OneShotReturn.hpp` into `Native/Systems`. In a System source include it and
register a PostPhysics System with Animator write access. The helper takes the
callback's stage and stable scene/entity UUIDs; it keeps no separate clock.

```cpp
#include "OneShotReturn.hpp"
void forge_register_system_WalkReturn(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::Animator>("forge.animator");
    world.system("gameplay.WalkReturn")
        .kind(host->post_physics_phase)
        .write<forge::Animator>()
        .run([host](flecs::iter& it) {
            forge_return_on_animation_completion(
                host, it.world(), "65d69337-571f-4574-9504-b8a632316d79",
                "137bd0b6-b8c1-49c8-b957-65e2339eeebb", 1);
        })
        .add(host->fixed_tag);
}
```

Register this hook and source with the same project start/target_sources pattern
as MarkerPulse above; allow Animation capability. These UUIDs identify scene11's
Walk character. Use your own saved UUIDs when adapting it to another scene.
In Assets/MarkerPulse.states.json, turn off Walk's Loop and Save/Publish. Select
Walk's Animator state2, save and rebuild the C++ project. Play: Walk finishes and
C++ requests Idle state1; it becomes active at the next synchronization boundary.
The helper requires an explicit state ID and respects a newer state request,
including a request for the entry state0. The compiled behavior exports
with the matching module kit. Actual SDK integration checks this helper using a
nonloop fixture, not only a syntax example.

## Optional per-character Play, Pause and Restart

Copy `ClipControl.hpp` into `Native/Systems` and include it in a System source.
This example takes three action UUIDs from your project's existing input map.
Use digital actions and replace the three arguments in your startup call with
those saved action UUIDs. Pressed edges apply once; held input does not restart
every tick. The System controls every registered Animator it queries.

```cpp
#include "ClipControl.hpp"
#include <flecs.h>
#include <forge/animation_components.hpp>
#include <string>
void forge_register_clip_controls(const ForgeSdkWorldV1* host,
                                 const char* play_action, const char* pause_action,
                                 const char* restart_action) {
    const std::string play = play_action, pause = pause_action, restart = restart_action;
    flecs::world world(host->world);
    world.component<forge::Animator>("forge.animator");
    world.system<const forge::Animator>("gameplay.ClipControls")
        .kind(host->fixed_phase)
        .each([host, play, pause, restart](flecs::iter& it, size_t row,
                                        const forge::Animator&) {
            const forge::sdk::Client client(host);
            ForgeSdkActionV1 action{};
            uint32_t operation = 0;
            if (client.action(play.c_str(), action) && action.pressed)
                operation = FORGE_SDK_ANIMATION_PLAY;
            if (client.action(pause.c_str(), action) && action.pressed)
                operation = FORGE_SDK_ANIMATION_PAUSE;
            if (client.action(restart.c_str(), action) && action.pressed)
                operation = FORGE_SDK_ANIMATION_RESTART;
            if (operation)
                forge_control_clip(host, it.entity(row).id(), operation);
        })
        .add(host->fixed_tag);
}
```

Register the source/start hook using the MarkerPulse target_sources pattern above;
pass the three action UUID strings to this function. Allow Animation capability in
Native/gameplay.cpp, ORing existing permissions. Input is an intrinsic query, so do
not put FORGE_SDK_INPUT in descriptor permission fields. Keep the FORGE-created
module dependency list unchanged; its managed builder already owns that declaration.
Save, build, then Play. Turn Play on Start off to test explicit Play, and use a
nonloop state to test Restart after completion. Enabled must be on and Speed must
be positive for advancement. A pending Animator state change wins at synchronization.
The helper controls the current admitted clip, never invents identity from names or
stores a native handle. The real matching C++ integration fixture executes this
helper and verifies pause/resume/restart through recovery in another process.

## Seek the incoming animation

Copy `ClipSeek.hpp` into `Native/Systems` and include it from an ordinary Gameplay
System. Call `forge_seek_clip(host, entity.id(), seconds)` on an input pressed
edge. It uses the entity's registered identity and the admitted incoming clip's
seconds. It retains clip pause and fade, skips events/root movement across the
jump, and returns false for an unavailable player or rejected position.

Use the duration reported by `Client::animation_playback` to bound your request.
The loop end maps to zero. An outgoing named state's clock remains independent.
The Inspector's tinted Live animation section offers the same temporary playhead
adjustment during editor Play. Rebuild and export project code with matching SDK
kits; the game ships compiled C++ and needs no editor or compiler installed.

### Select a skeletal blend subtree

`SetBlendSubtree.hpp` updates the existing typed Animator from an ordinary C++
Gameplay System. Pass a durable ModelNodeAsset reference belonging to that imported
skeleton; an empty reference blends the whole rig. Assign compatible primary and
secondary clips first. The engine retains phase and its current fade. Rebuild your
module against the matching SDK after the Animator layout update.

## Choose a blend or additive overlay

`SetAnimationMix.hpp` changes the existing Animator mode, weight and fade duration
from a C++ fixed System before Animation. Mode0 blends absolute poses; mode1 adds
offsets from the imported rest pose. It returns false for invalid input or missing
secondary clips. Assign both clips first. Combine it with `SetBlendSubtree.hpp`
to select an upper-body overlay without changing the playback phase. The actual
native SDK integration fixture compiles and executes these helpers, including
morph curves and fresh shared-worker recovery. Use the SDK from the same editor
build and rebuild your module after updating FORGE.

## Move a skeletal reach or aim target

`SetRigTarget.hpp` updates an existing typed `forge::TwoBoneIK` or `forge::AimIK`
from your ordinary fixed C++ System before Animation. Assign the durable bone
references and enable the component first. Targets are skeleton model-space meters;
the helper rejects missing components and nonfinite/out-of-profile coordinates.
Choose nondegenerate target/pole directions. The matching native SDK fixture
executes animated reach targets through real shared workers and recovery.
