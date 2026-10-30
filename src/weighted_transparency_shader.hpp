#pragma once
#include <stdexcept>
#include <string>
namespace forge {
// Wrap only our generated ordinary color entry, after its material lighting/fog.
// Authored surface programs and their input interface remain unchanged.
inline std::string weighted_color_wrapper(std::string source, bool surface) {
    const std::string declaration =
        surface ? "float4 ForgeSurfaceColor(ForgeSurfaceInput input):SV_Target0 {"
                : "float4 main(ForgeVarying input,bool front:SV_IsFrontFace):SV_Target0 {";
    const auto at = source.find(declaration);
    if (at == std::string::npos)
        throw std::runtime_error("Weighted transparency lost its generated color entry");
    source.replace(at, declaration.size(),
                   surface ? "float4 ForgeWeightedFragment(ForgeSurfaceInput input) {"
                           : "float4 ForgeWeightedFragment(ForgeVarying input,bool front) {");
    return "#include \"ForgeTransparency.fxh\"\n" + source +
           (surface ? R"(
ForgeWeightedTargets ForgeSurfaceWeightedColor(ForgeSurfaceInput input) {
    return ForgeWeightedColor(ForgeWeightedFragment(input),input.World);
}
)"
                    : R"(
ForgeWeightedTargets main(ForgeVarying input,bool front:SV_IsFrontFace) {
    return ForgeWeightedColor(ForgeWeightedFragment(input,front),input.World);
}
)");
}
} // namespace forge
