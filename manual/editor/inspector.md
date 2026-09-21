# Inspector

Inspector follows one explicit selection: an authored entity, a project asset, or a prefab source member. Selecting an asset clears entity selection. A prefab member selection points you to the independent **Prefab source** window; editing a scene instance is a different task.

## Edit an entity

Select an object in **Hierarchy** or **Scene**. Inspector shows its name, parent, local transform, appearance and attached components. **Name** commits with Enter. **Details** contains the abbreviated persistent entity ID and **Copy ID** copies its full value.

Position, Rotation and Scale have labeled X/Y/Z fields. Drag an axis or Ctrl-click to type; release commits one scene Undo step. Escape cancels a drag. Position uses meters, rotation uses Euler degrees, and scale is a multiplier. A move owns translation only; it does not override inherited rotation or scale.

**Spatial binding** chooses Follow parent, World or an explicit attachment. **Parent** preserves world placement and then follows the chosen parent. Cycles and unrepresentable shear reject without changing the scene. See [Transforms](transforms.md) and [Hierarchy](entities-hierarchy.md).

Inspector authoring controls are read-only during Play. Scene retains the authored view; Game displays runtime results.

## Add and find components

Click **+ Add Component** and search a component name or category. The list comes from FORGE's registered schema. Results have category headings. Already attached components are marked **Added**, including inherited components.

Attached components have collapsible headers. **Filter attached components...** appears when four or more optional components are attached (or a filter is active). It searches existing components; Add Component searches components to add. Transform and identity stay above the filtered list. Right-click a component header for **Remove component**, or **Revert component** on a prefab instance.

## Edit a property

Booleans use checkboxes, motion uses Static/Kinematic/Dynamic choices, integer values use integer fields, and decimal values use numeric fields with units. Numeric/text component edits commit with Enter. An invalid edit retains the previous good value, shows an error beside the field, and creates a Problems entry.

Asset fields show paths rather than requiring UUID entry. Use their picker/search, clear them, reveal them in Content, or drag a compatible Content asset onto them. See [Content browser](content-browser.md).

## Prefab intent and Revert

Prefab properties show **Inherited** or **Overridden** from explicit ownership/override intent. A value equal to its source can still be overridden. **Revert** follows the source again and supports scene Undo/Redo. Whole-component overrides have **Revert component**; independent translation, rotation and scale each have their own Revert.

The lower **Prefab instance** section shows revision/status and **Open prefab source**. **All override operations** is an advanced summary, not another source-editing transaction. Publishing source changes is outside scene Undo. Apply to Prefab remains deferred.

## More operations

Use the **Entity** menu or Hierarchy context menu to duplicate or delete. Transform reset, snap, ground placement, and color/shape commands are searchable in the [Command palette](commands.md). Ground placement aligns the preview mesh to Y=0; it does not query terrain.

Materials have their own [central editor](materials.md). Multi-selection and arbitrary plugin inspectors are not implemented.

Ordinary fields read label then value; narrow panels stack the label to preserve usable field width. Transform utilities are in its **three-dot** header menu.

## Metadata, units and ranges

Field labels and help use the registered component metadata. Physical quantities show applicable units; local translation is in meters, while quaternion rotation components and scale are dimensionless. Enum fields use named choices. Recommended ranges produce advisory feedback; unsupported hard limits reject the edit. Cross-field rules, such as maximum audio distance being at least minimum distance, still apply.

Integer properties reject fractional values and numbers outside their storage range.
Rejected edits keep the previous value and do not add an Undo step. Unrecognized
extension data already saved with a component is retained when known properties
are edited; it does not become an editable field merely because it was preserved.

## Nested values, lists and flags

Expand a structured property to edit its named fields. Arrays show a fixed number
of entries; lists also offer **Add item...**, **Remove item**, and **Move up**.
Large collections show sixteen entries per page. A new entry stays in a draft popup
until **Add** passes validation. **Cancel** leaves the collection unchanged.
The collection is one property: each committed edit is one scene Undo step, and
prefab Revert applies to the complete collection rather than one list entry.

Bit flags allow several named choices at once. Selecting a zero flag clears the
set. Integer fields retain their declared width; strings are not truncated to a
short display buffer. Read-only metadata disables editing.

Right-click a property control for **Reset to default**. This assigns the current
schema's declared default. On a prefab instance it records an explicit override;
**Revert** instead follows the shared prefab value.

A component whose schema is missing or incompatible is labeled unavailable or
shown with a read-only explanation. Its stored data remains preserved. It does not
become editable merely because a similarly named component exists in another build.
The separate exact-SDK authoring admission workflow is still under implementation;
these generic controls do not automatically admit arbitrary C++ types.
