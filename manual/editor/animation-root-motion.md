# Let an animation move its object

Root motion takes movement from an imported animation and uses it to move the
object through the scene. This keeps the visible model with its object instead
of letting the skeleton move away inside it.

## Try the Gallery

1. Open `FeatureGallery / Scenes / 36-animation-root-motion.scene.json` and press **Play**.
2. Compare **Off - motion stays in skeleton**, **Apply - entity traces a square**, and **Extract - neutral pose for C++**. Off repeats movement inside its model. Apply moves and turns the object around a square. Extract holds its pose in place and makes movement available to C++.
3. Stop Play. Select the middle object's top-level root in the Hierarchy. In Inspector's **Animator**, find **Root motion** and **Root joint**.
4. Choose **Apply** to move this object from its animation. **Root joint** identifies the imported skeleton root. Leave it empty only when the skeleton has one root; this example explicitly selects **Rig root**.
5. Save, press Play again, and export the scene through the normal Export Game workflow. The exported game uses cooked animation data and does not need the original glTF or a compiler.

Existing scenes default to **Off**. Speed, looping, clip blends and named state
transitions still use the Animator's ordinary playback settings. Pause holds
movement. Restart begins the clip again from the object's current position.

## Use movement with C++ and physics

Choose **Extract** when your C++ controller should decide how the movement affects
physics. FORGE removes root movement from the visible skeleton and lets C++ read
the current simulation step's movement. It does not move the object automatically.

The included `CodeExamples/RootMotionKinematic.hpp` shows an unparented, unit-scale
object with a Kinematic Body. A C++ system runs after animation and queues the
physics target before physics advances. Compile such code with the matching SDK;
export then includes the compiled module through the ordinary project workflow.

An active Navigation Agent or a simulation-controlled body already has a movement
owner. Apply reports a conflict when it would replace that movement. Use Extract
and your controller's physics or navigation policy. Some turns on nonuniformly
scaled objects cannot preserve their scale; FORGE reports that case instead of
distorting the object. The original full-model import path is required for root
motion; the older animation-only conversion path supports Off.

See [animation](animation.md), [blending](animation-blending.md),
[states](animation-states.md), and [standalone export](runtime-content.md).
