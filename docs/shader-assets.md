# Shader assets — Phase7 implementation in progress

The shader subsystem currently has source/permutation admission, immutable cooked
value transport and a Diligent compiler/reflection adapter undergoing Windows
validation. Source capture, supervised worker and shared cache/publication integration are
implemented and undergoing validation. Content editing, material compatibility,
shader reload and production renderer consumers are still being integrated. This
page does not describe a shipped end-user shader workflow.

The source now also includes the explicit [material surface interface](surface-shaders.md),
with engine-owned geometry, color/depth pixel roles and immutable Material snapshots.
Its native acceptance remains pending with the combined Phase7 work package.

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
`d3dcompiler_47.dll`. The inner compiler key excludes asset identity and display names. The outer
publication key hashes the original authored document, including its AssetId,
so publication can recheck the exact source. Neither key uses source timestamps. Cached/runtime bytecode retains reflection; stripping it is unsupported.

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
A shader document declares a canonical project `source_root`, such as `Shaders`.
The capture reads `.hlsl`, `.hlsli`, `.fx`, `.fxh` and `.inc` files recursively beneath
that directory, with a 4096 directory-entry limit. Filesystem links within the tree
are rejected. Stage paths and local include names are relative to this source root.
Host-provided `engine/` includes occupy a reserved virtual namespace. Project files
cannot shadow it. Engine include bytes contribute to the importer revision; project
source dependencies use real project locators in the shared dependency graph.
Changing an unused captured include also rebuilds: this is conservative invalidation.

The dedicated Windows shader worker reuses the fixed import-process transport and
supervisor. It creates a private Diligent WARP device for compilation, independent
of the editor's adapter/device. The supervisor enforces 1 GiB process memory,
60 seconds wall time, 50 seconds CPU time and bounded output. Cancellation retains
an explicit 100 ms grace period before terminating a compiler that cannot cooperate.
These process bounds supplement source limits; they do not promise a sandbox against
trusted native dependencies. No HLSL source executes inside the editor compiler path.
Native worker execution is still being verified on Windows.

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

## Publication ownership

The authored shader document owns its AssetId before compilation. Import refuses
an ID/catalog mismatch; copying a shader document requires the normal explicit
new-asset identity operation. A selected permutation lives in the shared import
settings sidecar. The compiler adapter revision includes exact FORGE source,
toolchain and build configuration fingerprints, alongside the actual FXC DLL digest.

The existing AssetImportService, DerivedDataCache and AssetPublisher handle queueing,
cache admission, source rechecks and journaled catalog/sidecar publication. Shader
preparation checks its inner compiler key on fresh output and cache hits. A failure,
cancellation, stale include or rejected consumer compatibility leaves the previous
catalog selection intact. Compatibility remains an explicit caller preflight; this
foundation does not yet connect production material pipelines to shader publication.
Shader asset history remains separate from scene Undo.

## Selected CPU resources

`request_shader` copies the typed catalog selection and loads its validated DDC
artifact through the existing ResourcePool. Source documents and HLSL includes are
not consulted at runtime. Publication generation, compiler input key and reflection
layout must agree with the copied selection. A bad replacement fails its ticket
and preserves existing leases. This CPU resource path does not create Diligent
objects; native realization and material/pipeline compatibility remain separate
render-owner checks.
