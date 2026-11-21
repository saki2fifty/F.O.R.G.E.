#include "material_shader.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace forge {
std::array<std::array<float, 4>, 2>
material_texture_transform_rows(const MaterialTextureSlot& slot) {
    const auto c = std::cos(double(slot.rotation)), s = std::sin(double(slot.rotation));
    return {{{float(c * slot.scale[0]), float(-s * slot.scale[1]), slot.offset[0], 0},
             {float(s * slot.scale[0]), float(c * slot.scale[1]), slot.offset[1], 0}}};
}
std::vector<MaterialParameterUniform> material_parameter_uniforms(const MaterialData& values) {
    std::vector<MaterialParameterUniform> result;
    result.reserve(values.parameters.size());
    unsigned row = 1; // Header row: alpha cutoff and derived dielectric F0.
    constexpr unsigned row_bytes = 4 * sizeof(float);
    for (const auto& [name, parameter] : values.parameters)
        result.push_back(
            {name, parameter.type, row++ * row_bytes, material_parameter_width(parameter.type)});
    return result;
}
SamplerState sprite_frame_sampler(SamplerState state, TextureFilter mip) {
    state.u = state.v = state.w = TextureWrap::ClampEdge;
    state.min_lod = 0;
    state.max_lod = 1000;
    state.mip = mip;
    return state;
}
MaterialShader material_shader(const PbrMaterialProfile& material,
                               std::span<const unsigned> uv_sets, MaterialSamplerBinding binding,
                               bool sprite_profile, std::optional<TextureFilter> sprite_mips,
                               bool fixed_values) {
    return material_shader(material, uv_sets, binding, sprite_profile, sprite_mips, fixed_values,
                           {});
}
MaterialShader material_shader(const PbrMaterialProfile& material,
                               std::span<const unsigned> uv_sets, MaterialSamplerBinding binding,
                               bool sprite_profile, std::optional<TextureFilter> sprite_mips,
                               bool fixed_values, const MaterialShaderScope& scope) {
    MaterialShader result;
    const auto canonical = prepare_pbr_material(material.values);
    const auto& values = canonical.values;
    const bool frame_mips = sprite_profile && sprite_mips.has_value();
    const auto require = [](bool ok, const std::string& why) {
        if (!ok)
            throw std::runtime_error("Material shader: " + why);
    };
    const auto identifier = [](std::string_view text) {
        const auto letter = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        };
        return !text.empty() && letter(text.front()) &&
               std::all_of(text.begin() + 1, text.end(),
                           [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
    };
    require(scope.symbol_prefix.size() <= 64 &&
                (scope.symbol_prefix.empty() || identifier(scope.symbol_prefix)),
            "invalid generated symbol prefix");
    require(scope.sampler_names.empty() || binding == MaterialSamplerBinding::NamedElements,
            "shared sampler names require named-element bindings");
    const auto symbol = [&](std::string_view value) {
        return std::string(scope.symbol_prefix) + std::string(value);
    };
    const auto uniform = symbol("g_MaterialValues");
    const auto sampler_variable = symbol(material_sampler_variable);
    require(std::is_sorted(uv_sets.begin(), uv_sets.end()) &&
                std::adjacent_find(uv_sets.begin(), uv_sets.end()) == uv_sets.end() &&
                uv_sets.size() <= 19,
            "UV-slot map is invalid");
    result.uniforms.push_back({values.alpha_cutoff, canonical.dielectric_f0, 0, 0});
    std::string functions = "float " + symbol("ForgeAlphaCutoff") + "(){return " + uniform +
                            "[0].x;}\nfloat " + symbol("ForgeDielectricF0") + "(){return " +
                            uniform + "[0].y;}\n";
    for (const auto& binding : material_parameter_uniforms(values)) {
        const auto& name = binding.key;
        const auto& parameter = values.parameters.at(name);
        const auto width = binding.width;
        const auto index = binding.offset / (4 * sizeof(float));
        result.uniforms.push_back(parameter.value);
        static constexpr const char* lanes[]{"", ".x", ".xy", ".xyz", ".xyzw"};
        functions += "float" + (width == 1 ? std::string{} : std::to_string(width)) + " " +
                     symbol("ForgeParameter_" + name) + "(){return " + uniform + "[" +
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
        const auto texture = symbol("g_MaterialTexture") + std::to_string(index);
        auto state = slot.sampler;
        if (frame_mips)
            state = sprite_frame_sampler(state, *sprite_mips);
        auto [selected, inserted] =
            samplers.try_emplace(state, static_cast<unsigned>(samplers.size()));
        const auto sampler_slot = selected->second;
        if (inserted)
            result.samplers.push_back(state);
        auto sampler = sampler_variable + (binding == MaterialSamplerBinding::NamedElements
                                               ? "_" + std::to_string(sampler_slot)
                                               : "[" + std::to_string(sampler_slot) + "]");
        if (!scope.sampler_names.empty()) {
            require(sampler_slot < scope.sampler_names.size(), "shared sampler name is missing");
            sampler = scope.sampler_names[sampler_slot];
            require(sampler.size() <= 64 && identifier(sampler), "invalid shared sampler name");
        }
        result.textures.push_back({role, texture, uv_slot, sampler_slot, slot});
        declarations += "Texture2D<float4> " + texture + ";\n";
        const auto offset = result.uniforms.size();
        const auto transform = material_texture_transform_rows(slot);
        result.uniforms.insert(result.uniforms.end(), transform.begin(), transform.end());
        // KHR_texture_transform uses offset + rotation * scale * UV; source UV
        // orientation is unchanged. Hardware sRGB views own color conversion.
        const auto sampling = sprite_profile && !frame_mips
                                  ? texture + ".SampleLevel(" + sampler + ",transformed,0)"
                                  : texture + ".SampleGrad(" + sampler + ",transformed,dx,dy)";
        const auto uv_function = symbol("ForgeUV_" + role);
        functions += "float2 " + uv_function +
                     "(float2 uv){\n"
                     "float3 u=" +
                     uniform + "[" + std::to_string(offset) + "].xyz;\nfloat3 v=" + uniform + "[" +
                     std::to_string(offset + 1) +
                     "].xyz;\n"
                     "return float2(dot(u.xy,uv)+u.z,dot(v.xy,uv)+v.z);}\n";
        functions += "float4 " + symbol("ForgeSample_" + role) +
                     (frame_mips ? "(float2 uv,float2 dx,float2 dy,out bool valid){"
                                 : "(float2 uv,out bool valid){") +
                     "float2 transformed=" + uv_function + "(uv);";
        if (!frame_mips)
            functions += "float2 dx=ddx(transformed),dy=ddy(transformed);";
        functions += "valid=all(isfinite(transformed))&&all(isfinite(dx))&&all(isfinite(dy));"
                     "float4 value=0;if(valid){value=" +
                     sampling + ";";
        if (frame_mips)
            functions += "if(value.a>0)value.rgb/=value.a;else value.rgb=0;";
        functions += "valid=all(isfinite(value));}"
                     "return valid?value:0;}\n";
    }
    require(scope.sampler_names.empty() || scope.sampler_names.size() == result.samplers.size(),
            "shared sampler names do not match canonical sampler slots");
    if (!scope.sampler_names.empty()) {
        std::set<std::string_view> names;
        for (const auto& name : scope.sampler_names)
            require(names.insert(name).second,
                    "distinct canonical sampler slots require distinct shared names");
    }
    if (scope.sampler_names.empty() && binding == MaterialSamplerBinding::NamedElements) {
        for (unsigned i = 0; i < result.samplers.size(); ++i)
            declarations +=
                std::string("SamplerState ") + sampler_variable + "_" + std::to_string(i) + ";\n";
    } else if (scope.sampler_names.empty() && !result.samplers.empty())
        declarations += std::string("SamplerState ") + sampler_variable + "[" +
                        std::to_string(result.samplers.size()) + "];\n";
    require(result.uniforms.size() <= 128, "material uniform block exceeds the built-in profile");
    if (fixed_values) {
        result.source =
            "static const float4 " + uniform + "[" + std::to_string(result.uniforms.size()) + "]={";
        for (const auto& row : result.uniforms) {
            result.source += "float4(";
            for (unsigned lane = 0; lane < 4; ++lane) {
                if (lane)
                    result.source += ',';
                result.source += nlohmann::json(row[lane]).dump();
            }
            result.source += "),";
        }
        result.source += "};\n";
    } else
        result.source = "cbuffer " + symbol("ForgeMaterialValues") + " {float4 " + uniform + "[" +
                        std::to_string(result.uniforms.size()) + "];};\n";
    result.source += declarations + functions;
    return result;
}
} // namespace forge
