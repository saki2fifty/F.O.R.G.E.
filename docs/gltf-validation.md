# glTF fixture and validator evidence

## Pinned official corpus

[The test corpus](../tests/fixtures/gltf-official/README.md) contains18 unmodified
fixtures from Khronos glTF-Sample-Assets revision
`c6a6bd13ab2b3c685c7903d03561b8a9392f38b8`. It complements the existing
[NegativeScaleTest](../samples/gltf/NegativeScaleTest/README.md). Every source,
buffer, image and legal record has a recorded SHA256 and immutable download URL;
asset bytes were checked against the upstream Git blob inventory before inclusion.
The fixture notices retain authorship, CC-BY/CC0 licensing and applicable marks.
No branding or endorsement is inferred from these test assets.

| Area | Official fixture |
| --- | --- |
| GLB and basic geometry | Box/glTF-Binary |
| External textures/PBR | BoxTextured |
| Normal maps/tangent generation | NormalTangentTest |
| Opaque, mask and blend | AlphaBlendModeTest |
| Multiple primitives and morph targets | MorphPrimitivesTest |
| Skeletons, binds and animation | RiggedSimple, SimpleSkin |
| Node animation | BoxAnimated |
| Cameras | Cameras |
| Multiple punctual lights | PointLightIntensityTest |
| Sparse accessor data | SimpleSparseAccessor |
| Quantized animated morphs | AnimatedMorphCube/glTF-Quantized |
| Draco compression | Box/glTF-Draco |
| Advanced material layers | CompareClearcoat, CompareIridescence, CompareSheen, CompareTransmission, AnisotropyStrengthTest |

`gltf_official_corpus` uses the actual model importer's supported-extension set,
pinned Diligent native document/accessor implementation, mesh/material cooking,
native image decode, semantic variants, cooked round trips, skin influence/bind
admission, animation tracks, and camera/light metadata. It checks every fixture
hash before decoding and reports per-fixture counts, bytes and CPU timings.
CPU admission/cooking is distinct from rendered-image acceptance. Existing D3D12
WARP fixtures test rendering independently.

## Official validator comparison — verified2026-09-22

The comparison tool is the official
[2.0.0-dev.3.10 release](https://github.com/KhronosGroup/glTF-Validator/releases/tag/2.0.0-dev.3.10),
source revision[`bcd52cc4ba5f333b2999a58f67cc05ddf28b4fb1`](https://github.com/KhronosGroup/glTF-Validator/tree/bcd52cc4ba5f333b2999a58f67cc05ddf28b4fb1).
Its exact README, CHANGELOG, ISSUES, license and notices were inspected. This is
an external test/comparison utility, not an engine dependency, importer, runtime
API or schema authority. Upstream labels this official release a prerelease;
FORGE does not rename it stable or change an engine pin to adopt it.

The Linux64 release archive is2,082,740bytes with SHA256
`168eba887964125abe17ae97899b38d0b3cfd73c266c78424c194929ddcbc522`.
No validator build or runtime library is shipped in FORGE. Reproduction takes an
explicit local official validator executable and an external report directory:

```sh
python tools/compare_gltf_validator.py /path/to/gltf_validator tests/fixtures/gltf-official /path/to/reports
```

The script verifies fixture/resource hashes, records the actual executable hash,
runs bounded per-file validation with external resources enabled, and preserves
all JSON reports and stderr. Errors fail the comparison; warnings and informational
messages remain visible. Normal CI does not download a floating validator or depend
on a hosted web service.

All18 fixtures returned exit0 and zero errors in the recorded comparison. Warnings
include generated tangent space for NormalTangentTest/CompareSheen and a non-root
skinned mesh for RiggedSimple. Informational results include unused objects/tangents
and unsupported Draco extension validation. The validator's Draco notice does not
prove compressed data safe; FORGE's pinned Draco admission/decode tests supply that
separate evidence. Do not equate validator success with full renderer correctness
or suppress FORGE's own bounds/ownership/domain checks.
