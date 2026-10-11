# C++ physics loads example

Copy `Examples/PhysicsLoads` to a writable location and open it as a project.
The combined Windows package supplies its matching compiled C++ module. Press
Play, then Pause after about two seconds: six cubes compare force, mass, impulse,
torque, angular impulse and an off-center push. Stop restores the saved scene.

Select a cube and adjust Body Loads or Physics Body mass in the Inspector. The
one-kilogram and two-kilogram cubes receive the same six-Newton force; the heavier
cube accelerates half as fast. The impulse cube receives one kick. The last cube
also spins because its push is off-center. These cubes have no gravity and coast
after their loads stop, so they eventually leave the camera view.

After the first physics step, Console reports each cube's actual linear speed in
meters per second, angular speed in radians per second, and whether it is active.
The force pair has different speeds because of its masses; the off-center impulse
also has angular speed. These are read from the running solver by the C++ System.

Content → Code shows the C++ Component and System. To edit code, stop Play, save,
then Build Project Code using the matching SDK and your installed compiler tools.
The example's README explains each property and source file.

For Export Game, use `runtime-kits/shared-native-sdk` and the project's current
compiled module kits parent. For the supplied example, select
`Native/Builds/example`, which contains the `project.gameplay` kit. Build selects
a new kit parent after rebuilding.
The exported `forge_game.exe` runs the already compiled C++ on Windows.
