#pragma once
#include <forge/shader_asset.hpp>
#include <stdexcept>
namespace forge::asset_detail {
// Derived publication facts, checked against the admitted cooked Shader. Source
// and program bytes remain authoritative; opaque metadata stays with its owner.
inline nlohmann::json shader_publication_metadata(const ShaderData& shader) {
    nlohmann::json result{{"version", 1},
                          {"layout", shader.layout_digest()},
                          {"compiler_input_key", shader.build_key}};
    if (shader.surface)
        result["surface_wrapper_version"] = shader.surface_wrapper_version;
    return result;
}
inline void validate_shader_publication_metadata(const nlohmann::json& metadata,
                                                 const ShaderData& shader) {
    const auto expected = shader_publication_metadata(shader);
    for (const auto& [key, value] : expected.items()) {
        const auto actual = metadata.find(key);
        if (key == "surface_wrapper_version") {
            if (actual == metadata.end() &&
                shader.surface_wrapper_version < first_explicit_surface_runtime_profile)
                continue;
            if (actual == metadata.end() || !actual->is_number_integer() || *actual != value)
                throw std::runtime_error(
                    "Shader runtime surface profile differs from cooked data; rebuild the Shader");
        } else if (actual == metadata.end() || *actual != value)
            throw std::runtime_error("Shader compiler/layout provenance differs from selection");
    }
    if (!shader.surface && metadata.contains("surface_wrapper_version"))
        throw std::runtime_error("Nonsurface Shader has unexpected runtime surface metadata");
}
} // namespace forge::asset_detail
