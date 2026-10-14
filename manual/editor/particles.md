# Mesh particle effects

A particle effect is a reusable stream or burst of small meshes. Use it for
stylized sparks, debris or fountains. This profile uses solid or masked meshes;
transparent smoke, trails and particle collision are not supported.

## Try the Gallery

Open **Scenes/54-cpu-mesh-particles.scene.json** in FeatureGallery. Press **Play**
to see orange and blue fountains and an initial burst. Stop and restart Play to
repeat the burst. Select an emitter in Hierarchy to change its seed, visibility,
layers or playback speed. Speed 0 freezes that emitter; disabling it restarts
emission when enabled again. Save authored changes for export.

Open **Scenes/56-particle-shapes-and-lifetime.scene.json** to compare Point, Box,
Sphere and Disc origins. The back pair compares a free jet with drag and a size
curve. Open effects under **Assets/Particles/Shapes** from Content to change the
shapes, damping, growth and color. These scenes need no C++ compiler.

## Create and edit an effect

1. Stop Play. In **Content → Create / Register**, choose **Particle Effect**.
2. Enter a project file such as `Assets/Particles/Sparks.particles.json` and create it. The **Particle Effect** document opens. Existing sources open from Content.
3. Choose its **Mesh** and optional **Material**. Start with the built-in cube.
4. Choose Continuous or Burst. Set capacity, lifetime and either particles/second or burst count. Enter numeric values to commit them. Continuous capacity must accommodate rate times lifetime; errors retain the previous valid values.
5. Choose Point, Box, Sphere or Disc. Box uses full dimensions in meters; Sphere fills its radius and Disc fills a plane perpendicular to Direction. Adjust direction, cone, speed, acceleration and drag. Zero drag preserves free motion.
6. Use preview Play/Pause, time and Reset. Preview is independent of game Play and does not change the scene. Orbit/zoom its image. Undo/Redo edits source values.
7. Choose **Save / Publish**. This saves and builds the runtime effect. A failed build reports the problem and retains the working published revision.

## Size and color over life

Use start/end size and color for a simple fade in size or RGB. Enable **Use size
curve** or **Use color gradient** for more control. Each channel starts with birth
and death stops; **Add stop** splits the largest gap. Stop time runs from 0 at
birth to 1 at death. Enter commits values.

- Edit each stop's size or linear RGB. Colors have no particle opacity.
- Choose **Linear** to blend to the next stop, or **Step** to hold until it.
- Move interior times between their neighbors; the birth/death times stay fixed.
- Remove interior stops. Each channel supports 2–16 stops.
- Disable a channel to return to its retained start/end values. Undo restores the previous stops and values.

Preview Play/Pause and time show the same motion and lifetime values used by the
exported game. Changes stay in the effect draft until **Save / Publish**. Existing
effects stay loadable; adding extended controls updates the source format in the
same Undo step, without changing its asset identity.

## Put it in a scene

Add **Particle Emitter** to an object through Inspector, then assign the published
effect. The effect document also has **Assign to selected Particle Emitter**.
The object can also have an ordinary MeshRenderer. Place a game camera and light,
save and press Play. Scene view uses a frozen sample while editing.

Changes made during Play are temporary live values. Stop to edit and save the
authored scene. Export through **Export Game** normally: selected effects, meshes
and materials are included already built. Players need no compiler.

## Size and tint for each object

Select an object with **Particle Emitter** in Inspector. Set **Size multiplier**
from 0.01 to 16 and **Tint R / G / B** from 0 to 1. A value of 1 keeps the effect's
original size or color channel; a tint of 0 removes that channel. These values
multiply the effect's lifetime values without changing motion. Several objects
can share one effect and have different sizes and colors.

During Play, the colored **Live gameplay** section offers these four controls.
They update the running effect without restarting it. Stop discards Live changes.
To keep values, Stop, edit the ordinary Inspector and save the scene.

Open Gallery **57-particle-emitter-appearance.scene.json**. The front row compares
small, normal and large neutral jets; the back row adds warm, green and blue tints.
All six use **Assets/Particles/SharedJet.particles.json**. Export and run normally.
Projects with C++ modules must rebuild those modules against the matching SDK.

## Limits

Each effect supports up to 1,024 particles; a scene supports 64 enabled emitters
and 8,192 reserved particles. Use one static triangle part, one LOD and one material
slot. Built-in opaque/masked PBR and Unlit surfaces work. Skinned/morphing meshes,
Blend/transmission and custom Shader surfaces report errors. This profile does
not cast particle shadows.

Changing the seed restarts the effect with a different pattern. C++ can adjust the
native component; `sdk/sample/particle_probe.cpp` demonstrates seed/speed changes
with a matching rebuilt SDK module.
