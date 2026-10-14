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

## Create and edit an effect

1. Stop Play. In **Content → Create / Register**, choose **Particle Effect**.
2. Enter a project file such as `Assets/Particles/Sparks.particles.json` and create it. The **Particle Effect** document opens. Existing sources open from Content.
3. Choose its **Mesh** and optional **Material**. Start with the built-in cube.
4. Choose Continuous or Burst. Set capacity, lifetime and either particles/second or burst count. Enter numeric values to commit them. Continuous capacity must accommodate rate times lifetime; errors retain the previous valid values.
5. Adjust direction, cone angle, speed, acceleration, size and start/end color. Motion follows the emitter transform. Color is RGB, without particle opacity.
6. Use preview Play/Pause, time and Reset. Preview is independent of game Play and does not change the scene. Orbit/zoom its image. Undo/Redo edits source values.
7. Choose **Save / Publish**. This saves and builds the runtime effect. A failed build reports the problem and retains the working published revision.

## Put it in a scene

Add **Particle Emitter** to an object through Inspector, then assign the published
effect. The effect document also has **Assign to selected Particle Emitter**.
The object can also have an ordinary MeshRenderer. Place a game camera and light,
save and press Play. Scene view uses a frozen sample while editing.

Changes made during Play are temporary live values. Stop to edit and save the
authored scene. Export through **Export Game** normally: selected effects, meshes
and materials are included already built. Players need no compiler.

## Limits

Each effect supports up to 1,024 particles; a scene supports 64 enabled emitters
and 8,192 reserved particles. Use one static triangle part, one LOD and one material
slot. Built-in opaque/masked PBR and Unlit surfaces work. Skinned/morphing meshes,
Blend/transmission and custom Shader surfaces report errors. This profile does
not cast particle shadows.

Changing the seed restarts the effect with a different pattern. C++ can adjust the
native component; `sdk/sample/particle_probe.cpp` demonstrates seed/speed changes
with a matching rebuilt SDK module.
