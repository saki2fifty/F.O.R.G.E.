# C++ physics velocity

Four original cubes launch upward at one meter per second and rotate around world
Z at one radian per second. They have no gravity or damping. After two seconds:

- Coast continues moving and rotating.
- Stop linear stays in place while rotating.
- Stop angular continues rising with its orientation held.
- Stop both stays in place with its orientation held.

Velocity Control in Inspector owns the launch vector, angular vector, stop time and
channel choice. Angular commands use radians per second. Changes to authored
values take effect on the next fresh Play; runtime progress preserves one-shot
intent during recovery. Console reports measured first-step and stopping speeds.

This is ordinary C++ project source. The combined Windows package includes its
matching compiled module, so copy the project to a writable folder and press Play.
After changing source, Build Project Code with the matching SDK and your installed
compiler tools. A source-only checkout needs that build before Play/export.
No compiler or editor is shipped in an exported game. Choose the shared native SDK
runtime kit and `Native/Builds/example` module kits parent when exporting the
supplied compiled project. A rebuilt project selects its new kit parent.
