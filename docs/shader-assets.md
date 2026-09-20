# Shader assets — Phase7 implementation in progress

The shader subsystem currently has source/permutation admission, immutable cooked
value transport and a Diligent compiler/reflection adapter undergoing Windows
validation. Project worker/publication, Content editing, material compatibility,
shader reload and production renderer consumers are still being integrated. This
page does not describe a shipped end-user shader workflow.

## Source and compilation profile

A `forge.shader` version1 source document has an AssetId and a list of stage,
source-file and entry-point records. Its original JSON is the authored document;
projection into compiler inputs does not remove unknown extension fields.
Fixed defines and named permutation choices are separate. A request selects one
value per declared axis; it never eagerly enumerates a Cartesian product.
Graphics programs require a vertex stage, hull/domain must appear together, and
compute programs are separate assets. A depth-only graphics program may omit pixel.

The initial native profile is Windows x64, D3D12, HLSL/FXC shader model5.1. It covers
vertex, pixel, compute, geometry, hull and domain compilation. Device-enabled
features gate the latter stages. Mesh/amplification/raytracing and DXIL are not
claimed. Pinned Diligent silently caps shader models and can fall back from DXC;
FORGE instead requests the explicit FXC profile and checks the compiled version.
No new compiler dependency or dependency upgrade is introduced.

Build identity includes stage source/entry points, exact captured source bytes,
selected defines, matrix packing, optimization, Diligent compiler-adapter revision,
backend/profile, compiler Debug flag and the digest of the actual loaded
`d3dcompiler_47.dll`. Asset identity, display names and timestamps are not content
keys. Cached/runtime bytecode retains reflection; stripping it is unsupported.

## Includes and bounded input

Diligent's native memory source factory exposes only an immutable captured source
set. Its native include implementation resolves parent-relative includes and runs
the real preprocessor, including conditional branches and include guards. There is
no fallback filesystem factory. Missing or escaping include paths fail compilation.
The build key currently includes all captured files, conservatively invalidating
when any changes; this is not yet a minimal compiler-discovered include graph.
Source discovery/watch and publication must retain the shared asset dependency
index; no private shader dependency or job registry is introduced.

The current admission profile allows256 source files,2MiB per file and16MiB total;
virtual filenames must be canonical, with ASCII case collisions rejected for
Windows portability. Embedded NUL and control characters are rejected. There are
at most64 fixed defines,16 permutation axes and32 distinct choices per axis.
These are bounded compile-request limits, not promises about hardware throughput.
Project-source compilation still needs its supervised worker before public use.

## Reflection and cooked data

Diligent supplies resource types, registers/spaces and recursive constant-buffer
member types, shapes and offsets. Official D3D12 reflection supplements resource
dimensions, thread-group size and the actual bytecode stage/version, which are
absent from Diligent's public descriptor. It cross-checks register/array bindings.
See the [official binding descriptor](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ns-d3d12shader-d3d12_shader_input_bind_desc)
and [shader version types](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ne-d3d12shader-d3d12_shader_version_type).

Copied reflection admits256 resources, bounded resource arrays,4096 total member
records,8 nested struct levels and64KiB constant buffers. Overlapping register
ranges, unbounded arrays and invalid member offsets/shapes reject. The reflected
layout digest includes stage visibility, packing and resource/member descriptors.
It is not a source digest, and code changes alone do not imply layout changes.
Full material compatibility remains a separate adoption check.

Cooked `FRGSHD` version1 envelopes contain stage bytecode, per-stage byte digests,
reflection, build/compiler provenance and a layout digest. Total bytecode is bounded
at16MiB and metadata at2MiB. Envelope validation checks ranges, lengths, hashes and
metadata before native realization. It is not a substitute DXBC parser or proof of
native bytecode validity. The native path creates all shaders into a detached
candidate, re-reflects them, and requires exact agreement before returning it.
Failure never changes the caller's selected program. Old pipeline/resource lifetime
must still follow the shared CPU lease/GPU fence contract when renderer adoption
is connected.

Shipping consumers load cooked bytes without invoking source compilation. Native
reflection still uses the platform D3DCompiler runtime already used by Diligent;
this is not a claim that the DLL can be removed from a working package.
