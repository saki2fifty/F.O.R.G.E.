# October 4, 2026

## C++ source onboarding in Content

- The Windows editor SDK acceptance fixture now searches Content for `RotationSystem` before opening the source file. In the compact bottom panel, the System row was below the visible list and a direct test click could not reach it. The search route exercises the browser behavior users can use in that layout.
- Build `261003-000145` passed Windows editor, core, SDK, standalone and compiler tests, including C++ creation, build and live Play. The final extracted-package onboarding stopped at the hidden System row. It was not delivered. The previous known-good Build `261001-000142` remains current until the corrected acceptance run passes.
