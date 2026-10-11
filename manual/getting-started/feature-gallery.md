# Explore the Feature Gallery

The packaged **Examples/FeatureGallery** project gives you eight short scenes to
open in the editor and export as standalone games. You can see how each effect is
authored, change it, then compare the result in Play and in the exported game.

Copy the whole gallery folder to a writable location before editing it. Choose
**File → Open project...** and select that copy. Use Content's scene list to open
**Transforms**, **Lighting**, **Physics**, **Cameras**, **Editable Mesh**, **Sculpt**, **Vertex Paint**, or **Transform Animation**. The project's `README.md`
gives a short experiment and expected result for each scene.

To export one, open and save that scene. In **Tools → Project Settings**, select
**Use saved current scene as startup** and **Save Settings**. Then use **Run →
Export Game...**, selecting the `runtime-kit` beside the packaged editor and an
output folder outside the project. Run the resulting `forge_game.exe`. Repeat
with another startup scene to export a different demonstration.

The gallery contains ordinary saved project content. It needs no C++ compiler. The included [reference game](../reference-game.md)
remains a separate combined gameplay example.

## Two-clip skeletal animation

Scene **09-animation-blending** compares Idle, an equal mix and Reach on three
skinned columns. Select the middle root and open **Preview skeletal animation...**
from Content to scrub or try smooth transitions. See [Preview and blend two animations](../editor/animation-blending.md). Both clips are included when you export this scene.
