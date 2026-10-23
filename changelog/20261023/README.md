# 2026-10-23 — MCP authoring development

Development source only. Build208 remains the latest delivered Windows package;
these changes have not been packaged or physically accepted on Windows.

## Native agent connection

- Added original C++ `forge_tools --mcp-stdio`, using the owning editor's authenticated loopback service and existing scene commands, validation and history.
- Supports MCP2026-07-28 and2025-11-25, bounded stdio/native transport, cancellation and editor replay receipts.
- Added **Copy MCP configuration** and documented setup/capability boundaries. Credentials remain temporary and outside project files/command-line arguments.

## Current-project scene files

- Added current project/document status plus explicitly granted Save, Save As, New and Open Scene tools through the existing file/document owners.
- Preserves ordinary/first-save/copy identities, disk-conflict checks, writer exclusivity and recovery cleanup. Dirty scenes need explicit discard before replacement; pending work and other source drafts block switching.
- File grants survive scene changes inside the current project with fresh scene targets and retained receipts. Scene-only grants still expire on scene changes; project changes revoke every grant.
- Filename changes now trigger normal editor scene activation, including selection/view handling.

## Project assets

- Added bounded asset discovery, import review/commit/cancel, reimport progress, deferred placement and reviewed duplicate/move/delete tools through the actual editor controllers.
- External imports stay within an explicitly selected source folder. File commits require the reviewed content digest; changed source bytes invalidate the review.
- Added GPU-independent editor-owner fixtures using real import workers, including animated glTF placement, cancellation, replay, stale reviews and project containment.

## Validation and remaining scope

Native Linux scene and document MCP process/socket tests, existing authoring/file
regressions and scaled ImGui controls pass. Captured protocol envelopes and tool
schemas are checked against the pinned official current/legacy specification.
Native Windows editor/client execution and a new delivery remain pending.

Project creation/switching, recovery controls, assets/domain documents, C++ builds,
Play/runtime/visual inspection and export are still separate MCP integrations.
See the [current capability checklist](../../docs/mcp.md).
