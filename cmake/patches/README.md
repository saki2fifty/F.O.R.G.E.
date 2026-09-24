# Maintained DiligentFX shader exception

Exact Engine `a279e5fa8593cbc758ec46ea1eba0b435cbc2f06` + Core
`744f079f61cdbda15d371383682418fc927e4a61` + FX
`aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da`, with this explicit FORGE patch set.
This is not a different dependency version. Native C++ remains unmodified.

The adjacent JSON records normalized-LF input SHA256 values and patch SHA256
`b2558b2620ad38bcf5270d8588d2b88b1e61242c132fcb3fd06be96b07b48151`.
`tools/stage_diligentfx_patch.py` checks exact git revisions, source bytes and patch
identity before applying the diff to disposable build inputs. It never changes
upstream sources. CMake embeds only the two staged replacements. All other native
shaders remain upstream. Staged files identify the modification in a comment;
upstream Apache2 licensing is retained in DiligentFX-LICENSE.txt.

## Semantics and upstream status

- `EvalIridescence`: initialize the TIR result and use one return. The formula,
  TIR result and branch behavior for NaN inputs are preserved. An explicit binary32
  NaN test accompanies `x >= 0`: FXC did not preserve the negated-comparison
  formulation for NaN in two differential GPU cases.
- `LambdaSheenNumericHelper`: use `pow(abs(x), c)`. All supported callers use
  nonnegative cosine-derived inputs, so their formula is unchanged. Unlike
  `max(x,0)`, `abs(x)` does not turn NaN into zero. No epsilon or authoring-domain
  change is introduced. FORGE's existing input rejection remains authoritative.

Official DiligentFX history inspected on2026-09-24 through
`6d900ba904bda33b3ec48980cebca00bf9e6db6f`. The only newer change touching these
files is `9746ac4c` (half-float support); both affected expressions remain unchanged.
No suitable upstream fix was found. The isolated compiler fixture and this diff
are sufficient starting points for an upstream report; no submission is required.

On every upgrade, recheck upstream, remove this patch if equivalent fixes exist,
or explicitly review a new patch. Revision/hash mismatches must fail closed.
Do not mechanically retarget the manifest.

## Identity and verification

CMake tracks patch, manifest, staging script and original shader inputs. The
staging identity includes manifest+patch bytes and is embedded into both modified
shader sources. Diligent's existing BY_CONTENT cache hashes included source.
The CI build-cache key includes patch files and staging code. Staging regression
checks idempotence, source/revision/hash refusal, and shader-byte invalidation.

Strict FXC debug/optimized, DXC DXIL/SPIR-V and native GPU validation are required.
The GPU differential fixture compares exact original code with patched code,
including TIR and NaN/Inf cases; its explicitly named **upstream differential**
shader intentionally retains original warnings as reference evidence. Production
shaders and isolated patched probes must not gain new warnings. Full material,
SampleGrad, optics/shadow captures and cache checks are required before delivery.
Current validation status is in docs/dependency-known-issues.md.
