# Make bones reach or aim

Open **Examples/FeatureGallery**, then open **40-two-bone-ik** or **41-joint-aim**
from Content → Scenes. Each scene has three copies: original pose, half constraint,
and full constraint. Select a copy's model holder in Hierarchy and inspect its
**Two Bone IK** or **Aim IK** component.

Two Bone IK bends a simple limb so its last bone reaches a position. Choose Start,
Middle and End from that model's imported bones. They must form a direct chain.
Set Target X/Y/Z and enable it. Aim IK turns a selected bone's Forward direction
toward a target. Up and Pole control its roll. These targets are measured relative
to the model, before the model's overall placement in the scene.

Weight0 leaves the animation pose; Weight1 applies the whole correction. Intermediate
weights blend the correction. A target beyond a limb's reach cannot stretch its
bones. The bend axis is local to Middle; the pole is a direction, not a position.
Keep the pole away from the target direction. Problems explains missing bones,
invalid chains and unavailable transform operations. Constrained bones can use
mirrored or uniformly scaled joints. Unevenly scaled/sheared joints cannot use
these constraints; the model's overall scene placement is independent.

Run Play or open the selected holder's Animator preview. Constraints adjust the
mixed animation before rendering. Animate the bones with C++ or clips; constrained
rotation paths without clip curves use the imported rest pose. Disabling/removing
the constraint restores those paths to their sampled base. Stop restores the usual
authored scene. **SetRigTarget.hpp** shows how C++ can move the target during Play.

Save and export the scene normally. The exported game uses the same compiled engine
constraint feature and cooked model data; it does not require glTF sources or a
compiler. Additional C++ project code ships as the normal compiled module.

This is a bounded reach/aim feature. Automatic retargeting, full-body IK and a rig
graph editor are separate work. Local automated runtime/export checks and physical
Windows acceptance are different; the latter remains pending for this local bundle.
