# 2026-10-20

## Tile Map foundation — validated source

Original Tile Set/Tile Map authoring, native C++ cells, copied Editor Play,
rendering and editable Gallery74 are implemented with focused local checks.
Windows core/SDK, full editor and shared runtime/export checks pass. Native
Tile Map controls were visually reviewed at three interface scales.
See [Tile Map details](tile-maps.md). These changes postdate Build261020-000194.

## Combined orthographic and Sprite source validation

- Orthographic Scene views, six axis directions, XY/XZ/YZ grids and saved view bookmarks.
- Visual Sprite sheet selection with pan/zoom, confirmation/cancel and ordinary Undo; Inspector, live values, Prefab drafts and Timeline retain their existing edit owners.
- Simple/Sliced/Tiled Sprite fill, pixel density and borders, with original Gallery73 examples and matching C++ SDK fields.
- Sequence-export progress stays within the resized window at all three tested UI scales. Published audio preview and initial source-import baseline corrections are included.

The [full Windows source audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39345839790)
passes 280/280 editor tests, standalone-only 2/2 and the 12-sample matched comparison.
Compiled source is `31759ff0f129e5a12abfca4a6d16d05337c693a9`; the workflow revision is
`f807a80cd1d615aefed6f7d6b51e1289c8e91ddd`. All 941 general input actions complete
in 209.20 seconds; all 117 sheet/fill actions complete in 84.94 seconds. Nineteen actual
WARP captures were visually reviewed. Modal and Cancel bounds fit both resized
windows at 100/150/200 percent. Image-export workflows preserve 263 protected source
files and existing asset identities after ordinary initial imports settle.

The [shared SDK/export audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39350795981)
passes at `2a7b5ff58a03695c914dd00b6368cca2ac140d8e`: 11 shared regressions, three
reference checks and a separate fresh runner. Gallery73 exports with the normal
shared kit; authored/kit copies are deleted before two relocated production
launches. The exact same manifest then passes two more production launches without
checkout/build/SDK on the fresh runner. Existing game save/settings relocation
checks remain. Production C++/headers/assets are identical to the full native
audit source; the intervening changes are CI and acceptance tests/budgets.

The [portable matrix](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39329462325)
passes Linux/Windows core 154 and SDK 187 each at source
`625fe5c5817f5c01df08975af821a09f95046a52`, whose portable implementation and pins
are unchanged by the subsequent editor/test corrections. Local normal/scoped
sanitizer and renderer evidence remains separately qualified in the Sprite contract.

Matched hosted process measurements report empty/cube responsive medians of
260.20/241.94 ms for the accepted older baseline and 348.37/343.92 ms for this source.
Settled working-set medians are approximately 61.0/61.4 MiB versus 71.3/74.4 MiB.
This is process responsiveness and memory evidence, not first GPU frame timing,
physical GPU FPS, smooth-frame-pacing acceptance or a speedup claim.

The general input walkthrough's aggregate CTest allowance is 480 seconds after a
previous slower runner reached 877/941 at the former 360-second cutoff. Its 12-second
action checks, 180-second compiler/graph checks and all assertions remain unchanged.
The successful full run above still used the stricter 360-second aggregate budget.

Build261020-000194 delivers these additions from source
`2d4c674c9491b2dccbb7ff168c75872d4a06f012`; its
[Windows delivery gate](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39355800878)
passes. Physical Windows/GPU, OS picker and
listening acceptance remain deferred. Public Sprite fields require rebuilding
native modules against the matching SDK; C ABI version 1 remains unchanged. This bundle adds original FORGE
code and artwork; existing attributed dependencies are unchanged.
