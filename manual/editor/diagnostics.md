# Scene diagnostics and component schema

These windows help you understand the data in your authored scene. Open them from **Tools** or the command palette.

## Scene diagnostics

The top of the window shows entity count, visible primitives, prefab templates, owned component count, and document revision. The report refreshes after a committed scene change.

Visible means an entity has an effective local translation, a resolved spatial binding, and is not a prefab template. It does not mean that the camera can currently see it. Owned components exclude values inherited from a prefab.

Findings are informational:

- Duplicate names are allowed because entity IDs remain distinct.
- An entity without effective local translation is not drawn in the preview.
- An unresolved spatial attachment hides the object until its binding is repaired.
- Unknown component data is retained when saving, but the current runtime does not implement it.

Click a finding to select its entity, then inspect it in Hierarchy or Inspector. Findings never automatically delete or repair anything. This window does not yet validate imported assets or standalone export requirements.

## Component schema

Expand a component to see its supported fields, stable property identifiers, numeric types, defaults, units, and limits. These describe the same built-in data checked by authoring commands.

The current components are LocalTranslation, LocalRotation, LocalScale, Tint, and Primitive. Rotation fields here are quaternion XYZW; Inspector presents Euler degrees. This window is read-only; use Inspector to change values. Animation-related metadata prepares property bindings for future tools; animation editing is not available in this build.

Continue with [Inspector](inspector.md) and [Command palette](commands.md).


Console → Gameplay input displays consumed runtime action values and edge counts. `forge_tools --document-schemas` lists the current document contracts without opening a window or writing files.
