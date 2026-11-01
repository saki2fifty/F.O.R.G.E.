# 2026-11-01

## Tile Map decoded-chunk residency

Scene and Game camera views drive background preparation and release of dense
32×32 Tile chunks through the existing resource pool. Multiple views reuse
identical pages; complete previous Map/image selections remain while edited
visible chunks prepare. Loaded resources reports Tile chunks separately.
Presentation bounds decoded pages and aggregate worker-input backing separately.
Per-chunk request failures preserve prepared bindings instead of repeating work.

Runtime and published presentation keep complete sparse logical Map values.
Offscreen cells, edits, neighbor rules, physics, copied Play and SDK queries retain
exact behavior. Edited checkpoints charge their independent dense reconstruction.
Cooked admission, worker verification and package checks share strict sparse
validation; explicit full loading remains available. Root identities, cooked1,
public SDK layouts and dependency pins remain unchanged. Export keeps the complete
Map. This is bounded decoded presentation residency, with no unlimited disk-world,
collision-unload or image-bank-streaming claim.

Visible-cell pose admission shares its visitor with emission. Partly visible dense
chunks can fit small pose budgets; oversized views reject and recover on a fitting
view. Local software-Vulkan checks are distinct from queued physical Windows/GPU
acceptance. No numbered Windows package accompanies this local batch.

Gallery126 supplies an original five-region Map and a looping camera Clip. It adds
one Scene and two authored assets through the existing document/publication owners,
preserving all 336 prior records and project settings. The manual includes current
loading/export steps and an authentic Loaded Resources component capture.
