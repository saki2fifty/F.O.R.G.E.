# September 30, 2026

## Editable Feature Gallery

- Added an ordinary editable sample project under `Examples/FeatureGallery` with separate scenes for hierarchy/transforms, three light types, falling Jolt physics bodies, and split perspective/orthographic cameras.
- Each scene has stable scene/entity identities and an authored game camera. The project contains complete standalone game defaults and uses only built-in resources; C++ tools are not needed to inspect or export it.
- Added a short experiment and expected result for each scene, a manual entry point, and a clear workflow to select a saved scene as startup and export it as a standalone game.
- Made `Examples/FeatureGallery/` a required directory in every future numbered Windows editor ZIP. Packaging now fails if the project, its scenes, or any gallery file would be omitted; source and final-package tests remain open to additional scenes. The reference game remains the combined gameplay example.
- Added authored-scene admission/identity regression and a packaged Windows test that copies, exports, and starts the gallery. Local scene admission passed; the Windows export/startup test has not run for this source yet.
