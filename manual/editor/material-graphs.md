# Material Graph

A material graph describes how a surface looks by connecting values and textures.
You edit the graph once, then use it in several materials with different colors,
textures or other parameters. It affects appearance; it does not control gameplay.

## Create a graph

1. In Content, open **Actions → Create / Register → New material graph...**.
2. Enter a new `.shader.json` path inside an existing project folder.
3. Choose **Create graph**. The Material Graph workspace opens with a Color parameter connected to Surface Output.
4. Select Color and change its Value. The independent preview updates after preparation finishes.
5. Choose **Save & Compile**. Wait for publication to finish before assigning the graph to a material.

The preview uses the same material renderer as the scene. Its geometry, camera and
lighting do not change your scene. Widen the workspace for a split view; in a narrow
workspace the preview stacks above the graph. Ctrl+Plus/Minus scales the interface.

## Connect nodes

Choose **Add node**, search by name or category, and select a node. Drag from an
output circle to a matching input circle. One output can feed several inputs;
each input accepts one connection. Right-click an input to disconnect it.

Ports are typed. A color and a plain vector have different meanings even when they
have the same number of channels. Add **Convert** when you intentionally change type.
**Combine** assembles channels; **Component** extracts one. Surface Output accepts
color/alpha, metallic, roughness, normal, emission and occlusion.

Select a node header to see its properties beside the canvas. Labels sit above
their controls so the properties remain readable in narrow workspaces. Drag the header to
move it. Ctrl-click selects several nodes. Delete removes selected nodes; Ctrl+C/V
copies/pastes them. A copied parameter becomes a separate parameter. Middle mouse
pans; the wheel zooms around the pointer; **Frame graph** fits the nodes.

## Select and arrange nodes

Use **Selection** for Select all, Clear or Invert. Drag a rectangle from empty
canvas space to select node bodies. Hold Ctrl to add or Alt to remove nodes.
Press Escape to cancel and restore the previous selection.

With nodes selected, open **Arrange**. Align their edges or centers horizontally
or vertically, space at least three centers evenly, or snap their top-left
positions to a grid. Grid spacing uses graph units, independently of zoom.
An applied layout is one Undo step; selection changes do not add Undo steps.
The same tools work inside functions.

Moving and arranging nodes keeps the current shader preview. **Save & Compile**
still saves their layout; changing values or connections updates the preview.

## Thresholds and rounding

Search **Add node** for these Math nodes, then choose their **Value type**:

- **Step** makes a hard cutoff: 0 below Threshold, 1 at or above it.
- **Smoothstep** makes a smooth transition between Low and High. Reversing those values reverses the transition; equal endpoints make a hard cutoff.
- **Ceiling** rounds upward: 1.2 becomes 2, and -1.2 becomes -1.
- **Round** selects the nearest integer. Halfway values go to the even integer: 1.5 becomes 2, 2.5 becomes 2, and -0.5 becomes 0.

For vectors and colors, each channel changes independently. Connect matching
types or use **Convert**. The nodes also work inside extracted functions.
These additions are locally tested; their Windows/GPU acceptance is pending.

## Magnitudes and ray directions

Search **Add node** for these Math nodes:

- **Length** measures a value's magnitude. A Vector2 of (3,4) gives 5; a scalar of -3 gives 3.
- **Distance** measures the gap between A and B. Both inputs must use the same type.
- **Reflect** calculates a Vector3 direction reflected around a surface normal.
- **Refract** calculates a Vector3 direction crossing between media. Ratio is the first medium's refractive index divided by the second's; total internal reflection gives a zero vector.

Length and Distance let you choose **Value type** and return a scalar. For colors,
they measure all channels, including alpha for Color4. Unconnected inputs are zero.
Reflect and Refract use Vector3 directions: provide unit Incident and Normal
vectors, using **Normalize** when needed. Refract's Ratio defaults to 1.
These nodes provide calculations you can connect elsewhere in your graph.
Their Windows/GPU acceptance remains pending.

## Parameters and material variations

Use Parameter nodes for values that individual materials should change. **Label**
is their readable name; renaming it keeps existing material values attached.
Constants describe values shared by the graph itself.

1. Create or open a material in Content.
2. Choose the compiled graph in **Surface Shader**.
3. Edit its named parameters and texture slots, then save the material.
4. Assign that material to a scene object's mesh slot in the Inspector.

An imported model can have a grouping root and separate mesh children. Select the
mesh child to assign its material. A flat mesh may disappear when viewed from its
back side; enable **Two-sided** in the Material workspace if both sides should show.

Create a material instance from an existing material to make a reusable variation.
Change only the values that differ. **Revert** follows the base value again.
**Edit graph** opens the assigned graph using that material for preview values.
Texture pickers inside the graph are preview choices only; save permanent texture
assignments in the Material workspace.

For a normal texture, use its data/normal usage, convert sampled channels to the
Normal Map node's vector input if needed, and connect Normal Map to Surface Normal.
The graph also supports texture arrays, cubes and volumes when matching assets exist.
A graph using only colors or parameters works on meshes without UV coordinates.
Planar textures and an unconnected Normal Map UV input require the selected mesh UV set.

## Reuse a function

Select a pure calculation with one outgoing result and choose **Extract function**.
Leave parameter/texture declarations and Surface Output outside the selection.
Use **Functions** to add calls or open a function's interior; **Back to surface**
returns to the main graph. Edit its name and press Enter to rename it.

Function inputs/output remain fixed after extraction. You can change its calculation
and connect additional calls. Recursive functions and incompatible signatures produce
errors instead of silently changing connections. Functions currently belong to this
graph asset; they are not separately published function assets.

Duplicating the Shader asset in Content creates a separate logical graph with new
node and function identities. Its parameter binding contract stays compatible;
opaque extension data is preserved. Compile the duplicate before using it.

## Save, undo and errors

Undo/Redo reverses graph edits independently of scene edits. Save & Compile writes
source and then prepares a compiled revision. An error keeps the previous usable
published material and preview. The retained image shows the last successful result,
not acceptance of the failed change. **Go to error node** selects the reported node,
including nodes inside functions. Correct the graph and compile again.

Closing with pending work offers Save, Discard or Keep editing. Discard drops unsaved
drafts; it does not erase source already saved by a failed compile. Unsupported
nodes remain in the source and show a diagnostic; they are not silently deleted.

After reopening the project, open the graph's Shader asset from Content. Exported
games use its compiled material through the normal [export workflow](runtime-content.md).
They do not contain the graph editor or require an installed shader compiler.
