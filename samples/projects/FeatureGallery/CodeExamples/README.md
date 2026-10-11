# Optional C++ marker action

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
