# October 3, 2026

## One project browser for assets and C++ files

- Content now shows supported project C++ source and header files under a friendly **Code** location while preserving their physical `Native/` paths. Component and System directories are ordinary subfolders. C++ files keep path identity; they do not receive AssetIds, import states, asset drag payloads or standalone source inclusion.
- Content search and type filters include C++ files. A real project folder named `Code` is disambiguated. Source discovery runs with the existing background Content refresh, retains prior valid results on scan failure, and clears old results on project switch.
- Single-click shows a read-only C++ file Inspector. Double-click, Enter and context Open focus the existing C++ Sources document. Creation through **Content → Create / Register → C++** uses the existing Component, System, source and header writers, shows the destination, refreshes/reveals the new row and opens its source. Empty Code shows a setup path.
- The old **Gameplay Code** panel is now **Build**. It keeps the `Native` dock identity and `panels.build` preference, compiler checks, build state, diagnostics, candidate admission and advanced setup, while the duplicate file browser and inline creation form are removed. C++ Sources keeps Save, Find, Build and per-file history, with a New shortcut into Content and Reveal in Content. Run offers the same Build Gameplay command.

## Compatibility, documentation and checks

- Migrates saved `Gameplay Code###Native` and legacy `Native` window labels without resetting custom dock geometry. The normal and reset layout still select Content at the bottom.
- Updated README, end-user manual, current technical guidance and editor/scripting architecture amendments for the new navigation. Expanded Content/layout and extracted Windows C++ onboarding fixtures to check file identity, creation, selection, editor focus, Build and standalone workflow continuity.
- Linux Content syntax and formatter checks, manual tests and package-test Python syntax passed. Build 143 compiled the Windows editor and passed core/standalone jobs but the general editor input workflow timed out at a 200% Content Actions step. The fixture now follows the established compact Actions route, including the new C++ creation steps; Build 143 was not delivered. Build 144 passed all source/core/editor jobs and the clean-room standalone check, then exposed a nested-menu popup timing defect in extracted C++ onboarding: the Component creation dialog did not appear. Modal opening now waits until the menu frame ends and stays pending until it begins. Neither failed build was delivered; numbered Windows package validation remains the delivery gate.
