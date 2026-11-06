# Original FBX examples

These small models and the checker image were authored for FORGE. They are
editable ASCII FBX7.4 sources, with no external model or animation library.

- `Static.fbx` references the adjacent `Checker.png` through a named UV set.
- `Moving.fbx` contains a one-second transform clip named `Move`.
- `Skin.fbx` adds a linear skin and a bone animated by the same clip.

Open `175-fbx-model-import` from the project's Scenes folder to run the prepared
placements. Import or reimport these models through Content/Model import to
inspect their separate typed Mesh, Material, Texture, Skeleton and Clip members.
Keep the adjacent image with `Static.fbx` when copying it outside this project.
