# 2026-10-25 — Illustrated manual and documentation audit

Local source changes; the existing Windows Build 213 ZIP is unchanged.

## Using FORGE

- Organized all 65 manual guides by task, added an editor orientation guide and
  linked the previously omitted physics-loads example.
- Added 37 authentic FORGE screenshots across 33 guides. The repository README
  shares the same canonical image files. Capture provenance remains in technical
  documentation, separate from user instructions.
- Corrected outdated animation, terrain, audio, runtime and export descriptions.
  Removed development and acceptance narratives from the affected user guides.
- Added the Camera and Light setup needed to test the first scene during Play,
  and linked that prerequisite from the C++ cube walkthrough.

## Offline manual

- Added local PNG support to the existing standard-library renderer. Screenshot
  bytes are preserved and included in the existing package manifest and ZIP.
- Navigation follows the manual contents, highlights the current page and stays
  bounded on narrow screens. Screenshots fit the article and open at full size.
- Added regression checks for local images, escaping, containment, relocation,
  task ordering and ZIP image/manifest inclusion.
- Documented the ongoing manual and screenshot maintenance workflow in
  [Maintaining the user manual](../../docs/manual.md).

Engine code, dependency pins, C++ ABI, persistence formats and MCP/automation
operations are unchanged. Native captures come from accepted prior Windows/Linux
validation; they are not new physical-hardware acceptance or performance results.
