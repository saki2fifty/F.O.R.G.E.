# October 1, 2026

## One C++ gameplay model

- Retired the movement-only `ForgeModuleV1` / ABI1 gameplay API, loader, C sample, Build & Reload UI, tooling and old CI/tests. Project gameplay now uses the exact rich SDK and Flecs components/systems. The exact SDK retains its own versioned binary entry and fingerprint checks. Code/schema changes use Stop, Save, Build and Play; there is no arbitrary in-place C++ reload.
- Preserved older unmarked `Native/gameplay.cpp` source on disk and report its previous module format as unsupported at Play/Export instead of silently ignoring it or rewriting arbitrary source. Existing rich-SDK `gameplay.cpp` projects continue to build.
- Added an installed managed registration header, separate Component/System source creation, a grouped project C++ browser, on-demand registered-system listing and an Inspector link to verified wizard-created component definitions. The Rotator sample works on a fresh entity with no authored LocalRotation override.

## Build, Play, Export and live values

- Added deterministic saved-source/build identity over bounded regular files under managed `Native/` plus the installed exact SDK fingerprint. Managed candidate publication checks source stability; editor status distinguishes dirty, required, building, failed, current and last-good. Build can explicitly save drafts first. Optional Build on Save remains.
- Play offers Save, Build & Play when source is dirty or the admitted module is stale. Export refuses dirty/stale managed gameplay and rechecks source immediately before publication, so a source edit made while export is preparing cannot publish the older module as current. Failed candidates leave the last good artifact intact without presenting it as current.
- Added typed, session/generation/entity-scoped Play property tuning through the existing runtime transport. Runtime validates and applies admitted reflected values at its owner-thread boundary, acknowledges the result, and keeps authored scene/prefab state unchanged. The Inspector labels transient live values separately from read-only authored values during Play.

## Windows acceptance and guide reconciliation

- Extended the extracted Windows editor/Developer Kit walkthrough to create and edit Rotator and RotationSystem through the built-in C++ editor, verify explicit Build and optional Build on Save, test a failed build and stale Play gate, and execute the edited System after two standalone relocations. The fixture now observes a typed live-value acknowledgement and compares fixed-tick rotation before and after Speed 90 → 360.
- Build 135 passed all source jobs but the final SDK onboarding fixture used an uninitialized project path; the editor had created and opened the file correctly. Build 136 then reached Play and exposed duplicate Dear ImGui IDs between live and authored component fields. The live field now has its own widget scope; starting Play also reveals it instead of retaining an Inspector scroll offset that hid most of the control. Build 139 proved live Speed tuning and fixed-tick motion, then exposed that opening a System from Gameplay Code left the Game tab in front. Opening any C++ source now brings its central document forward. No failed build was delivered.
- Reconciled the README, C++ gameplay manual and scripting ADR: removed obsolete C17 ABI/reload guidance and duplicated C++ editing steps, retained useful source-editor/compiler details, corrected the Editor Play session description, and clarified that export requires current saved code.
- The complete Build 140 C++ onboarding and two relocated standalone runs passed, but the separate reference-game acceptance exposed a repeatable paused-menu rebinding defect. Editor Play now delivers UI-generated neutral input before a rebind-start command and forwards replacement keys/mouse controls to the runtime listener ahead of RmlUi. The runtime-owned listener state is visible in input snapshots. No failed Build 140 package was promoted.

## Documentation and validation

- Updated the gameplay and Play manuals, README, native module contract and affected architecture/extension guidance for the single gameplay path. Updated the Windows onboarding fixture to create a C++ Component/System and exercise rotation and live speed tuning.
- Local current-source Linux builds and focused core/runtime/physics/audio/animation/navigation/collision/SDK tests passed. The installed SDK compiled and admitted generated Rotator/RotationSystem; a headless runtime step rotated an entity without authored rotation. The current-source Linux suite passed 96/96. Windows editor/visual/standalone package acceptance remains in progress until a numbered delivery is validated.
