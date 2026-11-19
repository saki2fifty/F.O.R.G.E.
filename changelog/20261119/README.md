# 2026-11-19

## Native save and graph validation

Scene source and asset-metadata publication share FORGE's native Windows
handle-rename helper. Source saves use supported replacement semantics that keep
existing readers on their prior snapshot and flush the staged file after rename.
Unsupported capabilities keep the existing fallback; real IO failures remain
visible. Higher-level source validation, writer leases, history and asset identity
stay with their existing owners. A native regression covers source, metadata and
Scene save/reopen, including held readers and failed destinations.

Graph format tags validate their string type before comparison, with malformed-tag
regressions for both material and particle domains. Native Windows acceptance is
recorded separately from local checks.

Particle graph fixtures and the editable Gallery example construct single-item
JSON arrays explicitly across supported compilers. The material regression also
constructs that example, and invalid function lists distinguish their JSON type
from the function-count limit. The manual uses the actual Functions menu to open
a reusable graph function.

## Authored particle graphs

Particle-domain Shader graphs control local position, size and linear RGB through
typed nodes and reusable pure functions. Effects select a published program and
declared bounds; CPU/GPU switching retains those settings. The existing compiler,
Shader worker, source/history, resource, renderer, export and automation/MCP owners
provide the workflow. Gallery239 includes an editable Orbit fountain and the manual
shows its real native canvas. Windows cooked execution remains pending acceptance;
no stateful feedback, collisions, trails or transparency is implied.

## GPU mesh particle effects

Particle Effects offer an explicit CPU/GPU Evaluation control. The GPU profile
creates birth inputs, evaluates shapes, analytic motion/drag and lifetime size/RGB,
and writes instance transforms on the device. Fixed clocks, logical assets, source
history, material/resource admission and renderer batching keep their existing
owners. Native source controls and automation/MCP use the same validated operation.
Older CPU sources and cooked effects remain readable with their original meaning.
New publications require a matching runtime kit; GPU effects require compute support.
Gallery238 contains an editable shared fountain example and the manual includes
its controls, export requirements and authentic rendered output. Authored stateless
graphs extend this profile separately; particle collision, transparency and trails
remain unsupported.

## Live movies across Scenes

**Run → Record game…** can follow game-requested Scene changes during current
Play with **Across scenes**. The existing recording observer retains a bounded
stereo PCM prefix across per-World audio retirement, tracks repeated visits and
keeps every saved image tied to its actual Scene. Preparation failures and the
64-visit limit stop recording while ordinary gameplay loading continues. Native
controls and automation/MCP share readiness, source/output guards and receipts.
Single-Scene live capture and fresh offline recording retain their existing
behavior. The manual includes current native controls and instructions.

## Runtime UI catalog observations

UI snapshots reuse validated catalog metadata when the current index bytes are
identical. Every observation still reads the current source or verified runtime
content and rechecks live path containment, physical aliases and subasset source
bindings. Malformed updates remain errors and are retried; retained catalog data
does not bypass UI admission. This avoids repeated metadata decoding without
creating a second catalog owner or changing asset identities.

Local focused native/transport/rendered checks are tracked separately from
Windows delivery, physical hardware and external player acceptance.
