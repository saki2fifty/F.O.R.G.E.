# 2026-10-29 — Per-tile collision profiles

Tile Set definitions support Full cell, None and convex polygons with 3–8
normalized XY vertices. Full cells retain merged rectangles; None tiles remain
visible without contributing collision. Custom shapes follow cell flips,
transpose, signed scale and rotation through the existing Static/Kinematic Tile
Map body. Sensors, queries, outlines and cold recovery use the same geometry.
Concave decomposition, Dynamic Maps and custom-polygon one-way collision remain
unsupported.

The Tile Set Inspector provides a numbered diagram, editable points and explicit
Apply/Discard controls. Unapplied edits gate source operations. The typed
`asset.document.tile_collision` operation and its discovered MCP binding share
source validation, revisions, permissions, history and Undo. Legacy source1
fields remain opaque; explicit guarded promotion to source2 preserves identities.
Cooked1 remains readable; new cooks use version2. Selected Map/Set revisions stay
paired across publication, previews, copied Play definitions and runtime packages.

Original Gallery101 demonstrates full-cell floors, mirrored ramps and decorative
tiles. The manual includes authentic Inspector and software-Vulkan captures with
recorded provenance. The private physics snapshot version is10; rebuild project
modules with the matching SDK. Native C ABI table signatures and asset IDs are
unchanged. Local validation remains separate from Windows/full editor/MSVC,
physical GPU and graphical exported-game acceptance. No numbered delivery is
issued by this change.
