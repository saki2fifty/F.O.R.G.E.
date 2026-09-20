#pragma once
#include <forge/material_asset.hpp>
namespace forge {
enum class PbrWorkflow { MetallicRoughness, SpecularGlossiness, Unlit };
struct PbrMaterialProfile {
    PbrWorkflow workflow{};
    // Derived copy with model defaults; never written back as authored overrides.
    MaterialData values;
    // Exact supplied parameter shape checked against model-owned declarations.
    MaterialLayout layout;
    float dielectric_f0 = .04f;
};
// This is the built-in material model boundary, not a generic shader schema.
// It preserves the existing identity-neutral cooked MaterialData representation.
PbrMaterialProfile prepare_pbr_material(const MaterialData&);
} // namespace forge
