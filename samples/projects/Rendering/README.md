# Rendering walkthrough

Original FORGE sample geometry and checker texture; no external artwork.
The source contains a textured cube, a copper material variant, a floor, a Game
camera and a directional light. It starts as an empty scene so you can try import.

1. Open this folder using **File > Open project**.
2. In **Content**, use **Create / Register > Import model...** and enter
   `Assets/Rendering.gltf`. Choose **Review settings**, then **Import / Reimport**.
3. After import, choose **Place model**. Frame the new root in Scene and press
   **Play** to view it through its imported Game camera. **Stop** returns to editing.
4. Select the placed model root. In **Inspector > Model Source**, try its material
   variant, then Undo. Scene Save keeps the placement and your edits.
5. Import `Assets/checker.png` as a Texture to inspect it independently. This makes
   a separate logical texture from the model's imported image member.

The model's generated materials are read-only source members. Create a new Material
in Content to try the Material Editor, then assign it through a placed mesh's
material slot. Edit the original PNG or glTF and reimport to try source updates.
Keep the generated sidecar and catalog with the project; `.forge` is disposable.
