# C++ component field animation

The cube rotates around its local Y axis. Its ordinary C++ Animated Spin component
exposes Speed in degrees per second. A reusable four-second clip eases Speed from
90 to 270 and back to 90. Enabled holds the cube still from one to two seconds;
Direction switches to Reverse from two to four seconds. The C++ system reads the
current samples in the existing post-animation phase and applies the rotation.

1. Copy this project to a writable folder and open it in FORGE.
2. A source checkout needs **Build Project Code** with the matching SDK and compiler
   tools before Play. The combined Windows package includes a matching compiled
   module; rebuild after changing C++ source.
3. Press **Play**, open Game and watch the cube change speed. During Play, select the
   cube and watch Speed, Enabled and Direction in **Live gameplay**. The authored
   values remain 90, enabled and Forward.
4. Stop Play and open **Assets/SpinSpeed.animation.json** in the Timeline. Choose
   **Animated cube** as Preview object. **Component fields** lists Speed, Enabled and Direction.
   Select a key, change **Field value**, and use **Add / Update key at playhead**.
   **Cubic field curve** and its numeric slopes shape Speed transitions. Enabled
   uses a checkbox; Direction uses its declared choices. These discrete values
   hold until the next key and have no curve controls.
5. Choose **Save & Publish**, save the scene and Play again. Negative key values
   reverse the selected direction. Timeline Undo/Redo edits the clip. Scene preview samples the
   numbers; actual C++ rotation runs during Play.

The component and system live in `Native/Components/AnimatedSpin.hpp` and
`Native/Systems/AnimatedSpinSystem.cpp`. The original source is also used by the
installed SDK sample. Float values interpolate; Boolean and integer/enum fields
hold until the next key. Native ranges and choices are validated. Nested, collection
and UI bindings are unavailable.

Export the saved project with the matching shared native SDK runtime kit and the
module-kits parent selected by the C++ build. The combined package's supplied kit
parent is `Native/Builds/example`; a local rebuild selects its new parent. The
exported game needs its compiled module and selected runtime clip. A C++ compiler,
Native source folder and editable animation source are unnecessary for players.
