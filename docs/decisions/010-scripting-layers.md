# ADR 010 — Three scripting layers

Date: 2026-09-19. Decision adopted for foundation planning; complete package
validation is pending. Future implementation requires its own authorized scope.

## Decision

Keep three distinct layers: exact native gameplay modules for C/C++ systems;
Flecs Script for ECS data/world construction and procedural recipes; a future
high-level gameplay language/visual scripting layer, deliberately not selected yet.
All ultimately use the same ECS authority and fixed runtime clock.

## Current evidence and implementation boundary

native_sdk.cpp, module_api.h and flecs_script.cpp implement the first two different
boundaries. The stable pin lacks the development asynchronous Script APIs and native
Meta maps; those are tracked rather than imitated.

## Consequences

The future layer must define sandbox/trust, VM ownership, deterministic modes,
asset identity, debug/reload and native bindings before adoption. Graph UI reuse
is permitted; a universal graph evaluator is not assumed. Native code remains trusted,
not a security sandbox.

## Deferred work and exact trigger

Select the high-level language only when an authorized gameplay-language work
package supplies concrete designer/debug/platform requirements. Reserve stable
component/property/command IDs and isolated runtime lifecycle now. No new foundational
dependency or language runtime is introduced by this architecture package.
