# 2026-11-11

## Asset-tools-disabled test dependencies

The Mesh texture-surface test keeps its CPU mapping checks in every profile;
its optional Gallery authoring command is compiled with asset tools. The two
audio runtime tests that publish source fixtures are registered with asset tools,
matching their publication-library dependency. Ordinary audio/runtime checks
remain in the core profiles. Full tools-enabled profiles retain all three cases.
Both tools-disabled profiles configure without undeclared FORGE link libraries.
The Mesh test compiles and passes with tools disabled; all three tools-enabled
cases also pass after rebuilding.

## Import and test portability

Audio, image decoding, atlas generation, movie encoding, source copying,
animation retargeting and foliage admission use explicit JSON operands when
comparing string views or computed strings. Navigation fixture provenance and
localization checks follow the same rule. These comparisons preserve their
existing type, revision and corruption checks. Prefab editor fixtures pass the
current Scene owner when opening a source, and the luminance presenter fixture
declares its direct PNG dependency. Eighteen focused native and MCP consumer checks
pass locally after rebuilding. Windows build and desktop acceptance remain pending.

## Shared artifact admission portability

The common selected-artifact admission helper uses explicit JSON operands for
string views and computed digests. Localization, application-state and render
texture consumers keep the same provenance and corruption checks. State-graph
layout admission uses the same explicit comparison for its format tag. Five
focused consumer checks pass locally. Release Ninja
jobs collect independent compiler failures in one pass and still require a fully
successful build before tests run.

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
