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

Select a node header to see its properties beside the canvas. Drag the header to
move it. Ctrl-click selects several nodes. Delete removes selected nodes; Ctrl+C/V
copies/pastes them. A copied parameter becomes a separate parameter. Middle mouse
pans; the wheel zooms around the pointer; **Frame graph** fits the nodes.

## Parameters and material variations

Use Parameter nodes for values that individual materials should change. **Label**
is their readable name; renaming it keeps existing material values attached.
Constants describe values shared by the graph itself.

1. Create or open a material in Content.
2. Choose the compiled graph in **Surface Shader**.
3. Edit its named parameters and texture slots, then save the material.
4. Assign that material to a scene object's mesh slot in the Inspector.

Create a material instance from an existing material to make a reusable variation.
Change only the values that differ. **Revert** follows the base value again.
**Edit graph** opens the assigned graph using that material for preview values.
Texture pickers inside the graph are preview choices only; save permanent texture
assignments in the Material workspace.

For a normal texture, use its data/normal usage, convert sampled channels to the
Normal Map node's vector input if needed, and connect Normal Map to Surface Normal.
The graph also supports texture arrays, cubes and volumes when matching assets exist.

## Reuse a function

Select a pure calculation with one outgoing result and choose **Extract function**.
Leave parameter/texture declarations and Surface Output outside the selection.
Use **Functions** to add calls or open a function's interior; **Back to surface**
returns to the main graph. Edit its name and press Enter to rename it.

Function inputs/output remain fixed after extraction. You can change its calculation
and connect additional calls. Recursive functions and incompatible signatures produce
errors instead of silently changing connections. Functions currently belong to this
graph asset; they are not separately published function assets.

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
