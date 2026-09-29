# September 29, 2026

## Phase10 — Material Editor and Material Graph

- Added typed graph Shader source, stable node/edge/function identities, source-located validation and bounded deterministic projection into shared PBR.
- Added central graph canvas, node search, properties, connections, copy/paste, reusable functions, source Undo/Redo, safe save/reopen and independent live preview.
- Added readable parameter labels with stable material binding identities; existing material instances and Revert remain authoritative.
- Reused the bounded Shader worker/publication pipeline, retained previous usable preview/revision on failure and fixed hidden background import submission/cleanup.
- Added graph document selection/Inspector context, actual native authoring/assignment/export workflow, all-node compiler coverage and relocated standalone graph fixture.
- Added identity-safe whole graph Shader duplication through existing Content file operations, preserving opaque data and compatible parameter bindings.
- Removed unnecessary UV-channel requirements for untextured graphs and cube/volume sampling; added actual UV-less mesh preparation/assignment coverage.
- Kept graph property labels readable in narrow columns and hardened native acceptance observations for unready UI models.
- Updated technical contracts, rendering decision and user manual.
- Completed Build 260929-000127 software validation: Linux/Windows core and SDK checks, native editor authoring, installed SDK workflows and package verification.
- Inspected native Windows captures of graph editing, diagnostics, material parameters and assignment to a cube and imported UV-less mesh.
- Verified graph material rendering after export, deleting the source project and moving the game installation twice; saves/settings and package integrity remained intact.
- Added explicit standalone graph-package coverage to future delivery and source-audit workflows. Physical GPU/DPI acceptance remains separate from WARP validation.

## Windows package layout

- Put editor executables, import/shader/navigation/UI workers and matching DLLs under `bin/`; keep one obvious root launcher.
- Move the exact C++ SDK, shared native runtime kit, developer launcher and reference game into an optional same-build Developer Kit ZIP.
- Bind the Developer Kit to the editor manifest hash and validate an overlaid relocated installation before either artifact is uploaded.
- Keep the static export runtime kit and offline manual in the ordinary editor package. Numbered delivery validation is pending.
