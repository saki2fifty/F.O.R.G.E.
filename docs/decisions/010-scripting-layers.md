# ADR 010 — C++ gameplay and integrated source editing

## 2026-10-01 gameplay consolidation amendment

The movement-only C17-compatible ABI1 gameplay path was retired by explicit
authorization. Exact-SDK C++ with shared Flecs is the sole project gameplay
model. Its versioned C-shaped entry is still a binary compatibility boundary,
not the old movement-only gameplay interface. Code and schema changes require
Stop → Save → Build → Play; source editing and live reflected-value tuning do
not introduce arbitrary native hot reload. The September compatibility sentence
below is historical. See [the current runtime contract](../native-modules.md).


Original decision: 2026-09-19. Revised by explicit user instruction: 2026-09-28.
This revision supersedes the proposed future high-level gameplay language and
visual-scripting layer. The integrated C++ editor is implemented and passed automated
Windows acceptance in delivered Build 260928-000125.
Physical hardware acceptance remains separate.

## Decision

C++ is FORGE's sole supported direction for gameplay programming. Do not introduce
Lua, another gameplay language, a gameplay VM or a separate visual gameplay language.
Provide a C++ source editor inside FORGE so users can create, open, edit, save,
build and diagnose project gameplay code without switching to an external IDE.
External IDEs remain optional. Existing native module, ECS and fixed-runtime
owners continue to execute compiled gameplay; source editing does not create a
second runtime or component authority.

The editor exists in FORGE only. Exported games must not contain a FORGE editor,
source-editing interface or authoring/compilation workflow. Existing runtime UI,
player settings and development diagnostics are not game editors.

## Existing implementation and compatibility

Phase9 provides C++ source generation, managed build, isolated admission and
export. The current authoring batch adds integrated source editing. Preserve existing
failed-build retention, process separation and exact-SDK compatibility checks.
Rich exact-SDK changes remain Stop/build/Play; do not imply arbitrary C++ hot reload.

Flecs Script remains the already accepted ECS data/world construction and recipe
facility (ADR003), not a second gameplay programming language. Shader source,
UI markup and serialized asset data likewise are not gameplay language choices.
This decision does not remove the C-compatible module ABI or break existing C17
compatibility; new user-facing gameplay onboarding and editor work target C++.

## Performance requirement

FORGE must remain performance-minded in both editor and exported runtime. Keep
compilation and substantial source analysis outside the UI frame path, reuse
incremental build outputs, and keep editor-only source tools out of runtime targets
and exports. Assess representative editing responsiveness, build latency, runtime
CPU/frame time and memory when affected. C++ alone is not evidence of performance;
use measurements and existing ECS/ownership contracts to justify optimizations.
Do not introduce a language runtime or duplicate build manager for this feature.

## Acceptance contract

Open/create project C++ files; edit/save with source undo/redo, syntax highlighting,
line numbers and search; build with the existing Gameplay Code owner; navigate
compiler diagnostics to file/line; inspect/attach admitted components and Play;
verify a bad build preserves the last good module. External edits and unsaved
source must have explicit conflict/save behavior. Verify editor responsiveness
and export a game that runs without editor UI, sources, SDK or compiler tools.

Completion-aware editing, debugger integration and more advanced IDE features
need separately defined scope and evidence; they are not implicitly delivered.
No editor widget/library or new dependency is selected by this decision.
