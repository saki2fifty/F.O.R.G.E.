# 2026-11-09

## Optional native UI text shaping

Project Settings adds **Shape UI text**, an optional saved boolean which keeps
existing rendering unchanged until enabled. The editor, standalone host and native
UI inspection worker use the same setting. Existing project-settings MCP tools
share native validation, stale-state, Save and discard behavior. Enabled projects
require matching runtime-kit support before export can replace a distribution.

An independently authored public font-interface adapter combines pinned shaping
and raster libraries for bounded horizontal glyph runs, including ligatures,
combining marks, explicit language/direction, tracking, font effects and whole-run
fallback. It retains existing resource admission and renderer lifetime ownership.
Paragraph bidirectional layout, full international editing and accessibility
remain separate work. Existing dependency pins and the installed native SDK ABI
are unchanged; the new shaping dependency is private to optional UI presentation.

The original **UiTextShaping** sibling project includes a Camera, interactive HUD,
project font and saved option. The illustrated manual explains controls, source
editing, saving, export and current limits. Package writers include sibling UI
documents, styles and fonts and adapt their manual links for the offline edition.
Local native, source-hidden export and software-rendered pixel checks are distinct
from pending Windows/full-editor, physical and external-client acceptance. No
numbered Windows package accompanies this source checkpoint.
