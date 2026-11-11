# 2026-11-11

## Windows build compatibility

Core compilation enables MSVC's larger object format for the expanded built-in
component registrations. Navigation profile and animation-binding comparisons
use explicit JSON operands to avoid ambiguous C++20 comparison candidates. The
parallel-worker MCP test is registered only when its SDK fixture target exists;
the non-SDK editor configuration keeps the remaining authoring workflows. The
matching asset-tools/MCP configuration and focused core, navigation-source and
transform-animation checks pass locally. Windows build and desktop acceptance
remain pending.

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
