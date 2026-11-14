# 2026-11-14

## Material Graph compilation ownership

Save & Compile cancels and retires active preview compilation before starting
Shader publication. The graph preview then admits the exact published cooked
Shader through the shared resource validation path, retaining material values,
texture assignments and source history. Unsaved previews retain their existing
worker path, and rejected replacements preserve the last usable preview.

Focused regression checks exercise real worker cancellation, publication-time
compile exclusion, cooked provenance rejection and project boundaries.

## Native authoring input checks

The native walkthrough creates and bakes a navigation surface, then assigns its
NavMesh through the agent's existing Inspector field before live tuning. Queued
text entry brings the entire input into view before clicking and typing. A
regression uses the actual Scene lighting owner in a scrolled window and verifies
the exact authored color values.

## Windows Gallery packaging

Shader publication waits respect the existing compiler budget. The package
preparer watchdog covers its sequential Shader, Material and UI inspection work,
with source-specific timeout diagnostics. Preparation failures retain the previous
archive and leave the source Gallery unchanged.
