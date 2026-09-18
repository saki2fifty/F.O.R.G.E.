# Problems and diagnostics

**Problems** retains actionable errors and warnings for the current editor session. **Console** records chronological status changes and low-level runtime/build details. A failed field edit also explains itself beside that field.

## Find and resolve an issue

1. Open **Window → Problems**, or look at the problem count in the status bar.
2. Select an entry to inspect its entity or asset when that context is available.
3. Read the property/source context, correct the input or file, then retry the operation.
4. Use **Clear resolved / dismiss** to dismiss displayed entries. This clears messages; it does not repair project data.

Messages are bounded to 256 displayed entries and retained across panel closure. Repeated observed diagnostics are deduplicated. Switching projects starts a new session list. Console retains the most recent 256 status changes; **Clear log** clears only the displayed log.

Build output is also available in **Gameplay Code**. Runtime UI, asset conversion/build and authoring failures feed Problems. Some low-level messages have no navigable target; use their source text and Console details.

## Scene diagnostics

**Tools → Scene diagnostics** provides informational scene findings, including duplicate names, missing effective transform values and unresolved spatial bindings. Duplicate names are allowed because identities remain distinct. Unknown component data is retained when saving, although the current runtime cannot implement unknown behavior.

## Component schema

**Tools → Component schema** shows registered components and their field identities, types, defaults, units and limits. These are the same schemas used by Add Component and the typed Inspector. Quaternion storage is shown here; authored rotation editing presents Euler degrees.

See [Inspector](inspector.md), [Native gameplay](native-gameplay.md) and [Play mode](play-mode.md).
