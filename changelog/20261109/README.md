# 2026-11-09

## Project font face discovery

Registered project fonts expose bounded per-face family, style, full and
PostScript names and recorded weight through the existing asset metadata.
Content's Inspector lists face indices, families, styles and weights; ordinary
asset reads expose the same information to automation and MCP. Unsupported or
malformed optional labels remain unavailable without rejecting an otherwise
admitted font. Refresh preserves file bytes and existing asset identities.

The collection example and illustrated manual teach index selection and alias
independence. This adds no system-font scan, variation controls, native font
selection changes, dependency upgrades or runtime-kit requirements.

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


## Optional whole-character native UI editing

Project Settings adds **Edit whole UI characters**, an independent saved boolean
for bounded plain Left/Right/Backspace/Delete in existing native text/password/
textarea controls. An original public-interface adapter prepares transient
Unicode18 extended-grapheme selections while preserving widget values/events,
disabled/max-length behavior, focus and the SDL composition path. Modified keys,
full international editing, accessibility and paragraph bidirectional layout
remain outside scope. Existing dependency pins and the installed gameplay ABI
are unchanged; exact utf8proc2.12.0 is private to optional UI presentation.

The editor, standalone and inspection worker share the setting; export requires
matching kit capability. Existing settings/source MCP tools retain their grants,
digests/history and Save rules. Runtime native field input is explicitly not an
MCP capability. The existing UiTextShaping example enables both options without
changing any of its four asset identities. Its illustrated usage guide covers
plain keys, local text, limits and export. No numbered Windows package is created.


## Native game UI field automation

MCP adds bounded native field discovery, focus, text and paired named keys through
the existing UiPresenter/Play owners. Native values, selection, events, limits,
platform input and camera surface mapping remain authoritative. Runtime input
requires current Play and displayed UI scope, a current presentation revision,
unique visible enabled fields and the execution grant. Read access omits password
plaintext. Input receipt replay is exactly once and UI reload invalidates old
presentation revisions. Transient field values never write authored source,
Scene history or settings.

A shared private native-field classifier and key catalog keep physical input,
automation discovery and MCP consistent. No arbitrary DOM/script/value setter or
IME injection is exposed. The existing UiTextShaping example and illustrated
manual describe agent field testing without changing its four asset identities
or static HUD capture. Both protocol envelopes and both Play profiles are tested;
full Windows/editor/physical/external-client review remains pending. No numbered
Windows package accompanies this source checkpoint.


## Native UI font collections

Project UI resources admit bounded TTC/OTC collections and native zero-based
face selection. Both font backends retain native layout/style ownership; the
optional shaped adapter selects the same face in raster and shaping libraries,
and identical collection bytes are shared across loaded aliases/indices.
Resource metadata reports face count and matching-kit requirements. Font files
also use the existing reviewed source-copy controller: font-containing batches
are copy only, preserve bytes and require explicit later UI registration. Native
and MCP workflows share those rules. Existing
catalog IDs, source closure and source-hidden content export remain authoritative.

Successful native/MCP UI registration and example creation now notify the existing
catalog-read owner, making saved resources visible without reopening the project.
Source operations, revisions, permissions, Scene history and native field input
retain their existing boundaries. UiFontCollections includes two project faces,
its notices, a Camera and native input; the manual explains selection and export.
Variable axes, named instances, color glyphs and full typography remain outside
scope. No dependency revision, installed gameplay SDK ABI or numbered Windows
package changes accompany this source work.
