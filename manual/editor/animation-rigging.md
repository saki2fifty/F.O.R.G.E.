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

Bone pickers and Content drops show only bones from the selected holder's model.
An existing missing or mismatched selection stays visible so you can correct it.
Disable the constraint before replacing its chain, then enable it again once the
chain is valid.

## Move a target in the Scene

1. Select the model holder and expand **Two Bone IK** or **Aim IK**.
2. Click **Edit target in Scene**. A labeled circle shows the target, with colored X/Y/Z handles. The Scene **View** menu and command palette offer the same tools.
3. Drag a colored axis. Its direction follows the model's placement, including parents and mirroring. Only the target changes; the model itself stays in place.
4. Release to apply. One Undo restores the whole drag. Escape cancels it.

Snap and Ctrl use the model's coordinate spacing. A collapsed or camera-facing
axis may be unavailable; orbit the camera or enter the target in Inspector.
Changing selection, moving/resizing the viewport or starting camera navigation
cancels an unfinished drag. Q/W return to Select/Move; R/S rotate/scale the object.
The circle previews your drag and animation preview stops. Reopen Animator preview
after applying the target to see the adjusted pose.
During Play the marker says **Live** and the handles are inactive. Adjust it with
the tinted Live Inspector instead. These guides are only in Forge.

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

During Play, select the holder and use the tinted **Live gameplay** Inspector to
adjust reach and aim. Press Enter to apply a numeric change. You can tune while
animation is paused; the pose updates without moving its playback position.
Invalid changes report a rejection and retain the previous values and pose.
These temporary changes disappear after Stop. To keep a change for export, Stop,
set it in the normal Inspector and save. A C++ system that updates a target may
replace your temporary value on its next tick.

Save and export the scene normally. The exported game uses the same compiled engine
constraint feature and cooked model data; it does not require glTF sources or a
compiler. Additional C++ project code ships as the normal compiled module.

This is a bounded reach/aim feature. Automatic retargeting, full-body IK and a rig
graph editor are separate work. Local automated runtime/export checks and physical
Windows acceptance are different; the latter remains pending for this local bundle.
