#pragma once
#include <string>
#include <string_view>

namespace forge {
// FORGE adapter around the selected native public lighting API. It reuses the
// completed layer state; no BRDF implementation or extra cubemap sampling.
inline constexpr std::string_view indirect_lighting_shader = R"(
float3 ForgeIndirectRadiance(SurfaceShadingInfo shading,SurfaceLightingInfo lighting) {
    shading.Emissive=0;
    lighting.Base.Punctual=0;
#if ENABLE_SHEEN
    lighting.Sheen.Punctual=0;
#endif
#if ENABLE_CLEAR_COAT
    lighting.Clearcoat.Punctual=0;
#endif
    return ResolveLighting(shading,lighting);
}
)";
// Built-in and cooked surface entries share the exact optional MRT order.
inline std::string indirect_target_shader(bool temporal) {
    return std::string("struct ForgeIndirectTargets {float4 Color:SV_Target0;\n") +
           (temporal ? "float4 Motion:SV_Target1;float4 Indirect:SV_Target2;\n"
                     : "float4 Indirect:SV_Target1;\n") +
           "};\n";
}
} // namespace forge
