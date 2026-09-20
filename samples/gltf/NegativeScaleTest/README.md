# NegativeScaleTest

Unmodified official Khronos glTF sample, used to test reflected hierarchy, face
winding and lighting. Copyright2023 Analytical Graphics, Inc.; created by
**Ed Mackey**. Licensed under [Creative Commons Attribution4.0 International](https://creativecommons.org/licenses/by/4.0/legalcode).

[Original model and explanations](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/c6a6bd13ab2b3c685c7903d03561b8a9392f38b8/Models/NegativeScaleTest).
The four source files are unchanged; [provenance](provenance.json) records their
exact SHA-256 hashes, sizes and immutable download URLs. This FORGE README and
provenance file are added separately. Preserve this attribution when redistributing
the fixture. No endorsement by the original authors is implied.

Native admission tests use the real model, its two PNG sources, geometry and
parent hierarchy. Rendering acceptance is a separate test; parser success does
not establish correct texture decoding, normal mapping, culling or lighting.
