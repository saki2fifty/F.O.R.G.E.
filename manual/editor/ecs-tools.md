# ECS inspection

**Tools → ECS → World inspection** opens optional developer tools for the current authoring world. These tools inspect native Flecs state. Ordinary scene editing continues through Hierarchy, Inspector and the authoring commands.

## Statistics

The **Statistics** tab shows entity, table, query, observer and system counts, estimated Flecs memory, world frame count and stage count. Counts include internal and diagnostic entities. These numbers are different from the editor's process memory and FPS in the status bar.

Opening the tool creates its diagnostic definitions. Module types are already registered consistently when worlds start. Sampling continues after closing its window for the rest of the editor session, so existing alerts can clear. It never advances gameplay or the Play process's clock.

## Queries and entity details

In **Query**, enter a native Flecs query expression or insert a registered **Component term**. Choose **Run query**, then **Next 100** or **First page** to navigate results. **Copy results** copies the current page's JSON.

A result row selects its authored entity when it belongs to the current scene. Native-only entities can still be inspected: open **Entity / JSON** to see the last selected result. **Inspect selected** reads the current Hierarchy selection. **Copy world JSON** copies native diagnostic data; it is not a scene save file.

Component IDs inserted by the picker belong to this world and session. Native query text treats dots as path separators; a literal dot in a registered name must be escaped, for example `forge\.local_translation`.

## Metrics and history

The Statistics tab includes an **Entity history** plot from Flecs' native statistics buffer. Its samples describe diagnostic sampling time rather than gameplay frames.

In **Metrics**, choose a **Source component**, optionally a numeric **Member**, and a **Kind**, then **Create metric**. Gauge reads current values. Counter reads an existing member counter, or measures how long a component is present. Counter increment integrates a numeric member over diagnostic time. Entity count totals matching component instances. Incompatible combinations show an error.

Values appear below the controls. **Remove** deletes the metric and its instances. These definitions last only for this editor session; they are not project assets or permanent status-bar items. Native historical statistics are also sampled for REST/Explorer without advancing the gameplay clock.

## Alerts and Problems

**Alerts** reports native Flecs alerts. Active issues also appear in **Problems** and clear when the condition resolves. Recommended ranges are advisory: audio gain above 1 is amplification, not automatically an invalid value. FORGE still rejects values outside supported hard limits when committing edits or admitting subsystem configurations.

## Optional REST and Explorer

In **REST / Explorer**, choose a **Port** and **Start REST**. The endpoint binds only to this machine at `127.0.0.1`. It is off at startup and is not enabled by saved preferences. **Stop REST** closes it and releases the port.

**Open Flecs Explorer** launches the official browser client configured for that endpoint. Browser local-network policy may require permission or a locally hosted client. The connection permits inspection only: Explorer scene edits, script execution and command capture are blocked. Use FORGE commands to change authored data.

The listener is a development tool; it is not a networked gameplay service or an alternative project writer.
