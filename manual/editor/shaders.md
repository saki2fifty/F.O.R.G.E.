# Shader import

A Shader asset is a prepared shader program. The current document compiles an
existing project `.shader.json` program and its HLSL files for the Windows D3D12
profile. This is a programmer-facing import workflow; it is separate from the
built-in surface models in the [Material editor](materials.md).

## Prepare and import a program

1. Put the `.shader.json` declaration and referenced HLSL/include files inside the project. Edit these files in your code editor.
2. Stop Play. Open **Content → Create / Register → Import shader...**.
3. Enter the declaration's project-relative path and choose **Review settings**.
4. Review its exposed permutation settings and choose **Import / Reimport**.
5. Wait for compilation and publication to finish. Diagnostics appear locally and in Problems.

The declaration owns the program's AssetId, stages, entry points and source root.
It must describe the supported source format; renaming a file does not allocate a
new logical program. The repository technical document `docs/shader-assets.md` describes the source
structure and limits.

Double-click the published Shader in Content to reopen its import settings. The
main Save action also imports when this document has focus. Closing with pending
settings uses the same Apply/Discard/Keep editing guard as texture/model import.

## When a build fails

Compilation runs in a separate worker. Unsupported declarations, invalid HLSL,
missing includes, stale inputs and incompatible compiled results do not replace
the previous published revision. Correct the files, reload settings when needed,
and retry. **Cancel import** rejects pending work.

Scene Undo does not undo file edits or shader publication. A successful import
proves a validated program was prepared; it does not automatically assign it to
scene materials or claim that every shader layout fits the mesh renderer.
