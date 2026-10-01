# October 1, 2026

## One C++ gameplay model

- Retired the movement-only `ForgeModuleV1` / ABI1 gameplay API, loader, C sample, Build & Reload UI, tooling and old CI/tests. Project gameplay now uses the exact rich SDK and Flecs components/systems. The exact SDK retains its own versioned binary entry and fingerprint checks. Code/schema changes use Stop, Save, Build and Play; there is no arbitrary in-place C++ reload.
- Preserved older unmarked `Native/gameplay.cpp` source on disk and report its previous module format as unsupported at Play/Export instead of silently ignoring it or rewriting arbitrary source. Existing rich-SDK `gameplay.cpp` projects continue to build.
- Added an installed managed registration header, separate Component/System source creation, a grouped project C++ browser, on-demand registered-system listing and an Inspector link to verified wizard-created component definitions. The Rotator sample works on a fresh entity with no authored LocalRotation override.

## Build, Play, Export and live values

- Added deterministic saved-source/build identity over bounded C++/header/CMake inputs plus the installed exact SDK fingerprint. Managed candidate publication checks source stability; editor status distinguishes dirty, required, building, failed, current and last-good. Build can explicitly save drafts first. Optional Build on Save remains.
- Play offers Save, Build & Play when source is dirty or the admitted module is stale. Export refuses dirty/stale managed gameplay. Failed candidates leave the last good artifact intact without presenting it as current.
- Added typed, session/generation/entity-scoped Play property tuning through the existing runtime transport. Runtime validates and applies admitted reflected values at its owner-thread boundary, acknowledges the result, and keeps authored scene/prefab state unchanged. The Inspector labels transient live values separately from read-only authored values during Play.

## Documentation and validation

- Updated the gameplay and Play manuals, README, native module contract and affected architecture/extension guidance for the single gameplay path. Updated the Windows onboarding fixture to create a C++ Component/System and exercise rotation and live speed tuning.
- Local current-source Linux builds and focused core/runtime/physics/audio/animation/navigation/collision/SDK tests passed. The installed SDK compiled and admitted generated Rotator/RotationSystem; a headless runtime step rotated an entity without authored rotation. The current-source Linux suite passed 96/96. Windows editor/visual/standalone package acceptance remains in progress until a numbered delivery is validated.
