# Aim a camera at an object

**Camera Aim** turns a Camera's view toward another Scene object. The camera stays
at its authored position, and meshes, lights and children on its entity keep their
ordinary transforms. Use it for a stationary camera watching a moving character,
or for a camera rig whose view should follow a target.

1. Create a Camera with **Entity > Create > Rendering > Camera**, or select an existing Camera in Hierarchy. Position it away from the target.
2. Choose **Add Component** in Inspector and add **Camera Aim** to the same entity.
3. In **Target**, choose the Scene object to watch. Choose an actual instance when working with Prefabs.
4. Leave **Enabled** on and **Weight** at **1** to face the target fully. **0** keeps the camera's ordinary direction; values between 0 and 1 apply part of the turn.
5. Adjust **Offset X**, **Offset Y** and **Offset Z** to watch a point away from the object's origin, such as a character's head. These offsets follow the target's local axes, rotation, scale and parents.
6. Select the Camera to see its aimed view volume in Scene. Press Play and open Game to see the result. Moving or animating the target updates the view.
7. Stop and save the Scene. Saved settings work in exported games with the matching runtime kit.

![Camera Aim Inspector with a Scene target, offsets and turn weight](../images/camera-aim-controls.png)

The view turns through the smallest angle from the camera's ordinary direction.
It follows the camera's roll through that turn; it does not lock the horizon or
add a delay. An exactly opposite target uses the camera's own up axis to choose
the turn. Perspective, Orthographic and imported camera directions are supported.
Changing **Weight** changes the angular turn, rather than the target's position.

During Play, **Live gameplay** exposes Enabled, Weight and the three offsets.
These values affect the running game. Stop and review **Keep runtime values** to
keep selected changes, then save. Assign or clear Target in Edit.

When arranging [Camera Shots](animation-sequences.md), each camera aims before
its view participates in a transition. A blended view can temporarily point
between the endpoint directions. Its attached objects keep their node poses.
[Camera render textures](render-textures.md) use the same aim settings.

Open **219-camera-aim** in the [Feature Gallery](../getting-started/feature-gallery.md)
and press Play. A yellow cube moves left to right while a stationary camera
follows it. Blue cubes and the floor provide a stationary reference. The example
uses a built-in transform animation and needs no project C++ module. Stop before
changing the target binding, and select this Scene as startup before exporting.

Clearing Target, disabling Camera Aim or setting Weight to zero leaves the
ordinary view available. A bound target must belong to the same Scene and have
a resolved transform. A missing target, coincident aim point or invalid camera
transform omits that camera and reports the reason in Problems. Select a valid
target or disable Camera Aim to restore it. Reflected, collapsed or sheared camera
frames remain invalid; aiming does not repair them. A target uses its node pose,
so aiming one camera at another does not create a recursive view dependency.
