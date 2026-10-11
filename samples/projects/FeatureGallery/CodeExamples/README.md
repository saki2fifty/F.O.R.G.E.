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
