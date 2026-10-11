# C++ physics velocity example

Copy `Examples/PhysicsVelocity` to a writable folder and open it as a project.
The combined Windows package provides matching compiled Project Code. Press
Play: four colored cubes move upward and spin. After two seconds, one stops moving,
one stops spinning, and one stops both. The remaining cube keeps moving and spinning.
Stop restores the saved scene.

Select a cube while stopped and edit its **Velocity Control** properties:

- `linear_x/y/z`: launch movement in meters per second.
- `angular_x/y/z`: launch rotation in radians per second; 1 radian/s is about 57 degrees/s.
- `stop_after`: seconds until the stop command.
- `stop_channel`: 0 keeps coasting, 1 stops movement, 2 stops rotation, 3 stops both.

`elapsed`, `launched` and `stopped` record running progress; leave their saved
defaults at zero/false for a new launch. Physics Body damping and speed limits
still apply. Zero launch speed stops that channel. The cube may still move from
gravity or collisions; this example turns gravity and damping off.

Console reports actual speeds once after launch and once after a timed stop.
Content → Code shows the Component and System. Stop Play before editing source,
then save and Build Project Code with the matching SDK and installed compiler tools.

For Export Game, choose `runtime-kits/shared-native-sdk` and the module kits parent
`Native/Builds/example` for the supplied module. A rebuilt project selects its new
kit parent. The exported game runs the compiled C++ and needs no compiler.
