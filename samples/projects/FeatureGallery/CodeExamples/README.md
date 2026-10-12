# Optional C++ animation behaviors

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
