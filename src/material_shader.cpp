#include "material_shader.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace forge {
MaterialShader material_shader(const PbrMaterialProfile& material,
                               std::span<const unsigned> uv_sets, MaterialSamplerBinding binding) {
    MaterialShader result;
    const auto canonical = prepare_pbr_material(material.values);
    const auto& values = canonical.values;
    const auto require = [](bool ok, const std::string& why) {
        if (!ok)
            throw std::runtime_error("Material shader: " + why);
    };
    require(std::is_sorted(uv_sets.begin(), uv_sets.end()) &&
                std::adjacent_find(uv_sets.begin(), uv_sets.end()) == uv_sets.end() &&
                uv_sets.size() <= 19,
            "UV-slot map is invalid");
    result.uniforms.push_back({values.alpha_cutoff, canonical.dielectric_f0, 0, 0});
    std::string functions = "float ForgeAlphaCutoff(){return g_MaterialValues[0].x;}\n"
                            "float ForgeDielectricF0(){return g_MaterialValues[0].y;}\n";
    for (const auto& [name, parameter] : values.parameters) {
        const auto width = material_parameter_width(parameter.type);
        const auto index = result.uniforms.size();
        result.uniforms.push_back(parameter.value);
        static constexpr const char* lanes[]{"", ".x", ".xy", ".xyz", ".xyzw"};
        functions += "float" + (width == 1 ? std::string{} : std::to_string(width)) +
                     " ForgeParameter_" + name + "(){return g_MaterialValues[" +
                     std::to_string(index) + "]" + lanes[width] + ";}\n";
    }
    std::string declarations;
    std::map<SamplerState, unsigned> samplers;
    for (const auto& [role, slot] : values.textures) {
        const auto found = std::lower_bound(uv_sets.begin(), uv_sets.end(), slot.uv_set);
        require(found != uv_sets.end() && *found == slot.uv_set,
                "material UV set has no mesh channel: " + role);
        const auto uv_slot = static_cast<unsigned>(found - uv_sets.begin());
        const auto index = result.textures.size();
        const auto texture = "g_MaterialTexture" + std::to_string(index);
        auto [selected, inserted] =
            samplers.try_emplace(slot.sampler, static_cast<unsigned>(samplers.size()));
        const auto sampler_slot = selected->second;
        if (inserted)
            result.samplers.push_back(slot.sampler);
        const auto sampler = std::string(material_sampler_variable) +
                             (binding == MaterialSamplerBinding::NamedElements
                                  ? "_" + std::to_string(sampler_slot)
                                  : "[" + std::to_string(sampler_slot) + "]");
        result.textures.push_back({role, texture, uv_slot, sampler_slot, slot});
        declarations += "Texture2D<float4> " + texture + ";\n";
        const auto offset = result.uniforms.size();
        const auto c = std::cos(double(slot.rotation)), s = std::sin(double(slot.rotation));
        result.uniforms.push_back(
            {float(c * slot.scale[0]), float(-s * slot.scale[1]), slot.offset[0], 0});
        result.uniforms.push_back(
            {float(s * slot.scale[0]), float(c * slot.scale[1]), slot.offset[1], 0});
        // KHR_texture_transform uses offset + rotation * scale * UV; source UV
        // orientation is unchanged. Hardware sRGB views own color conversion.
        functions += "float2 ForgeUV_" + role +
                     "(float2 uv){\n"
                     "float3 u=g_MaterialValues[" +
                     std::to_string(offset) + "].xyz;\nfloat3 v=g_MaterialValues[" +
                     std::to_string(offset + 1) +
                     "].xyz;\n"
                     "return float2(dot(u.xy,uv)+u.z,dot(v.xy,uv)+v.z);}\n"
                     "float4 ForgeSample_" +
                     role + "(float2 uv,out bool valid){float2 transformed=ForgeUV_" + role +
                     "(uv);float2 dx=ddx(transformed),dy=ddy(transformed);"
                     "valid=all(isfinite(transformed))&&all(isfinite(dx))&&all(isfinite(dy));"
                     "float4 value=0;if(valid){value=" +
                     texture + ".SampleGrad(" + sampler +
                     ",transformed,dx,dy);valid=all(isfinite(value));}"
                     "return valid?value:0;}\n";
    }
    if (binding == MaterialSamplerBinding::NamedElements) {
        for (unsigned i = 0; i < result.samplers.size(); ++i)
            declarations += std::string("SamplerState ") + material_sampler_variable + "_" +
                            std::to_string(i) + ";\n";
    } else if (!result.samplers.empty())
        declarations += std::string("SamplerState ") + material_sampler_variable + "[" +
                        std::to_string(result.samplers.size()) + "];\n";
    require(result.uniforms.size() <= 128, "material uniform block exceeds the built-in profile");
    result.source = "cbuffer ForgeMaterialValues {float4 g_MaterialValues[" +
                    std::to_string(result.uniforms.size()) + "];};\n" + declarations + functions;
    return result;
}
} // namespace forge
