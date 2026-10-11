# C++ physics contact example

Copy `Examples/PhysicsContacts` to a writable folder and open it as a project.
The combined Windows package supplies matching compiled Project Code. Press Play:
the red cube approaches the blue floor marker, enters its taller invisible sensor
volume, and reverses on the next physics step. The sensor does not block the cube.

Select **Contact actor** while stopped and edit **Contact Reaction**:

- `speed`: initial movement in metres/second; choose a positive value.
- `reverse_on_begin`: turn off to let the cube pass straight through the zone.
- `begin_count` and `end_count`: live counts of native contact observations.

Leave saved `launched`, `reacted` and counters at their initial false/zero values.
They record runtime progress for recovery. Stop restores the saved scene. Console
reports Begin/End with the actual physics tick. These are contact-cache events:
sleep or body removal can cause End without geometric separation, and complex
shapes may produce several events. This example uses one box pair.

Content → Code opens the original Component and System. Stop Play before changing
source, then save and Build Project Code with the matching SDK and installed tools.
For Export Game, choose `runtime-kits/shared-native-sdk` and module kit parent
`Native/Builds/example` for the supplied build. The exported game runs compiled
C++ and needs no source or compiler.

Physics Body speed limits still apply. The sample disables gravity and damping.
Use moderate speeds: a fast body can cross a sensor between discrete physics
samples; sensor bodies do not gain continuous response from the CCD checkbox.
