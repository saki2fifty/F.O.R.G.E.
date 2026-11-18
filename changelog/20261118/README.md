# 2026-11-18

## Physical XY character jump links

Grounded Character Controller 2D navigation can traverse explicit positive-lift
XY links between static physical supports. Native physics owns takeoff acceptance,
gravity, collision and descending landing. Navigation supplies bounded horizontal
steering and reports actual progress; a missed or obstructed landing cannot count
as arrival. Manual movement, zero-time synchronization and capsule replacement
retain the existing command priority and one-step lifetime.

Private physics14/navigation11 recovery preserves mixed physical3D/XY jump intent
without a second pose owner. Downgraded, malformed and empty extensions reject.
Editor controls, source history, automation/MCP and matching installed C++ SDK
consumers use the same implementation. The grounded route diagnostic identifies
an unsupported corner by index and XY coordinates.

Gallery228 includes two editable navigation islands, bidirectional positive-lift
links, static launch floors, raised landings and a physical obstruction. Source
and relocated export share the same behavior. The manual describes setup,
controls, saving and export. General platformer drop graphs and moving-platform
route planning remain unavailable. Physical Windows/GPU review remains separate.
