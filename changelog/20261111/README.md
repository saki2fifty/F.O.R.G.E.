# 2026-11-11

## Optional asset-worker test configuration

The texture-painting Gallery test is registered only when the asset-build worker
is enabled, matching the existing worker-backed Gallery tests. Core and Native
SDK configurations without asset tools no longer refer to a nonexistent target;
asset-tools configurations retain the test and its required worker dependency.
Formatting is reconciled with the pinned formatter, while the audio test fixture
keeps its required initialization include before the dependent test headers.
Both asset-tools-disabled Linux profiles configure successfully. The six focused
native tests, including the worker-backed painting Gallery, pass after rebuilding.
Windows package validation and desktop review remain pending.
