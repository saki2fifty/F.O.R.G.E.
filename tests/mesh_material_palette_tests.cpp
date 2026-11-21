#include "mesh_draw_shader.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Incompatible Material palette was accepted");
}
} // namespace
int main() {
    try {
        MeshVertexFetch fetch;
        fetch.normal = fetch.tangent = fetch.triangles = true;
        fetch.uv_sets = {0, 1};
        for (const auto* model : {"forge.gltf.unlit.v1", "forge.gltf.metallic-roughness.v1",
                                  "forge.gltf.specular-glossiness.v1"}) {
            MaterialData source;
            source.model = model;
            source.alpha = MaterialAlpha::Blend;
            source.depth_write = false;
            auto first = prepare_pbr_material(source);
            const auto role = first.layout.textures.contains("baseColorTexture")
                                  ? "baseColorTexture"
                                  : "diffuseTexture";
            source.textures[role].semantic = TextureSemantic::Color;
            first = prepare_pbr_material(source);
            source.textures[role].offset = {.3f, -.2f};
            source.textures[role].sampler.u = TextureWrap::ClampEdge;
            source.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                    {.2f, .4f, .8f, .6f}};
            std::array members{first, prepare_pbr_material(source)};
            const auto original = mesh_draw_shader(fetch, first);
            const auto palette = mesh_material_palette_shader(fetch, members);
            check(palette.members.size() == 2 && palette.samplers.size() == 2 &&
                      palette.members[0].uniforms != palette.members[1].uniforms &&
                      palette.draw.instanced && palette.draw.geometry.empty(),
                  "Palette lost per-Material values or shared sampler identity");
            check(palette.draw.pixel.find("g_ForgeMaterialIndex=input.MaterialIndex") !=
                          std::string::npos &&
                      palette.draw.vertex.find("uint MaterialIndex:ATTRIB7;") !=
                          std::string::npos &&
                      original.vertex.find("MaterialIndex") == std::string::npos &&
                      original.pixel.find("ForgePalette") == std::string::npos,
                  "Palette selection did not use a separate integer instance stream");
            if (first.workflow != PbrWorkflow::Unlit)
                check(palette.draw.pixel.find("ResolveLighting(s,lighting)") != std::string::npos,
                      "Palette bypassed shared physical lighting");
            check(mesh_draw_shader(fetch, first).pixel == original.pixel &&
                      mesh_draw_shader(fetch, first).vertex == original.vertex,
                  "Palette preparation changed ordinary Material shaders");
            members[1] = first;
            check(mesh_material_palette_shader(fetch, members).samplers.size() == 1,
                  "Identical native sampler states were duplicated");
            const std::vector four(mesh_material_palette_limit, first);
            check(mesh_material_palette_shader(fetch, four).members.size() == four.size(),
                  "Full Material palette was truncated");
            rejects([&] { mesh_material_palette_shader(fetch, {}); });
            const std::vector too_many(mesh_material_palette_limit + 1, first);
            rejects([&] { mesh_material_palette_shader(fetch, too_many); });
            for (unsigned mode = 0; mode < 5; ++mode) {
                auto incompatible = source;
                if (mode == 0)
                    incompatible.depth_write = true;
                if (mode == 1)
                    incompatible.depth_test = !first.values.depth_test;
                if (mode == 2)
                    incompatible.double_sided = !first.values.double_sided;
                if (mode == 3)
                    incompatible.alpha = MaterialAlpha::Opaque;
                if (mode == 4)
                    incompatible.textures[role].uv_set = 1;
                members[1] = prepare_pbr_material(incompatible);
                rejects([&] { mesh_material_palette_shader(fetch, members); });
            }
            auto incompatible = source;
            incompatible.textures.clear();
            members[1] = prepare_pbr_material(incompatible);
            rejects([&] { mesh_material_palette_shader(fetch, members); });
            for (unsigned mode = 0; mode < 5; ++mode) {
                auto geometry = fetch;
                geometry.skin = mode == 0;
                geometry.morph_count = mode == 1 ? 1 : 0;
                geometry.temporal = mode == 2;
                geometry.wind = mode == 3;
                geometry.triangles = mode != 4;
                rejects([&] { mesh_material_palette_shader(geometry, four); });
            }
        }
        MaterialData source;
        source.model = "forge.gltf.metallic-roughness.v1";
        source.alpha = MaterialAlpha::Blend;
        source.depth_write = false;
        const auto first = prepare_pbr_material(source);
        source.parameters["sheenColorFactor"] = {MaterialParameterType::LinearColor3,
                                                 {.4f, .2f, .1f}};
        std::array members{first, prepare_pbr_material(source)};
        rejects([&] { mesh_material_palette_shader(fetch, members); });
        source.parameters.erase("sheenColorFactor");
        source.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.4f}};
        source.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
        members.fill(prepare_pbr_material(source));
        rejects([&] { mesh_material_palette_shader(fetch, members); });
        source.parameters.clear();
        for (const auto* role :
             {"baseColorTexture", "metallicRoughnessTexture", "normalTexture", "occlusionTexture",
              "emissiveTexture", "specularTexture", "specularColorTexture", "clearcoatTexture",
              "clearcoatRoughnessTexture"})
            source.textures[role].semantic = first.layout.textures.at(role).semantic;
        const auto many_textures = prepare_pbr_material(source);
        check(mesh_material_palette_shader(fetch, std::span(&many_textures, 1)).members.size() == 1,
              "Single valid palette member was refused");
        const std::vector texture_overflow(mesh_material_palette_limit, many_textures);
        rejects([&] { mesh_material_palette_shader(fetch, texture_overflow); });
        unsigned slot = 0;
        for (auto& [role, texture] : source.textures) {
            (void)role;
            texture.sampler.max_lod -= float(slot++);
        }
        const auto sampler_overflow = prepare_pbr_material(source);
        rejects([&] { mesh_material_palette_shader(fetch, std::span(&sampler_overflow, 1)); });
        std::cout << "Shared Material palette preparation and incompatible-input guards passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
