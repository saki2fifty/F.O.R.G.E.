#pragma once
#include "mesh_shader_input.hpp"
#include <cstring>
#include <forge/shader_asset.hpp>
#include <forge/surface_shader.hpp>
inline void check_surface_shader() {
    using namespace forge;
    using Json = nlohmann::json;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto reject = [](const auto& action) {
        try {
            action();
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Invalid custom surface contract admitted");
    };
    SurfaceShaderDefinition definition;
    definition.uv_sets = {17, 0};
    definition.parameters["amount"] = {MaterialParameterType::Scalar, {.25f}};
    definition.parameters["tint"] = {MaterialParameterType::LinearColor3, {.2f, .4f, 2, 0}};
    definition.textures["color"].semantic = TextureSemantic::Color;
    definition.textures["color"].uv_set = 17;
    definition.textures["volume"].dimension = TextureDimension::D3;
    const auto document = surface_definition_document(definition);
    check(surface_definition(document) == definition,
          "Surface logical interface lost roundtrip intent");
    auto bad = definition;
    bad.uv_sets.push_back(17);
    reject([&] { validate_surface_definition(bad); });
    bad = definition;
    bad.parameters["not-an-identifier"] = {};
    reject([&] { validate_surface_definition(bad); });
    bad = definition;
    bad.textures["volume"].offset[0] = 1;
    reject([&] { validate_surface_definition(bad); });
    auto material = surface_material_defaults(definition);
    material.textures["color"].uv_set = 0;
    material.textures["color"].scale = {-2, 0};
    material.textures["color"].offset = {3, 4};
    validate_surface_material(material, definition);
    auto rows = surface_uv_rows(material, definition);
    check(rows.size() == 4 && rows[0][0] == -2 && rows[0][2] == 3 && rows[0][3] == 1 &&
              rows[1][1] == 0 && rows[1][2] == 4,
          "Surface UV selection/signed-zero transform changed");
    auto changed = material;
    changed.parameters["amount"].type = MaterialParameterType::Vector2;
    reject([&] { validate_surface_material(changed, definition); });
    changed = material;
    changed.textures.erase("color");
    reject([&] { validate_surface_material(changed, definition); });
    const auto field = [](std::string name, unsigned offset, unsigned width) {
        return Json{{"name", "g_SurfaceParameter_" + name},
                    {"class", width == 1 ? "scalar" : "vector"},
                    {"basic", "float"},
                    {"rows", 1},
                    {"columns", width},
                    {"offset", offset},
                    {"array_size", 0},
                    {"members", Json::array()}};
    };
    Json reflection{
        {"stage", "pixel"},
        {"resources",
         Json::array({{{"name", "ForgeSurfaceMaterial"},
                       {"kind", "constant_buffer"},
                       {"array_size", 1},
                       {"size", 32},
                       {"variables", Json::array({field("amount", 0, 1), field("tint", 16, 3)})}},
                      {{"name", "g_SurfaceTexture_color"},
                       {"kind", "texture_srv"},
                       {"array_size", 1},
                       {"dimension", "texture2d"}},
                      {{"name", "g_SurfaceSamplers"}, {"kind", "sampler"}, {"array_size", 2}}})}};
    const auto layout = surface_binding_layout(definition, reflection);
    const auto packed = surface_parameter_bytes(material, layout);
    std::array<float, 8> values;
    std::memcpy(values.data(), packed.data(), packed.size());
    check(values == std::array<float, 8>{.25f, 0, 0, 0, .2f, .4f, 2, 0},
          "Surface values ignored reflected packing or left padding uninitialized");
    auto invalid = reflection;
    invalid["resources"][0]["variables"][1]["offset"] = 0;
    reject([&] { surface_binding_layout(definition, invalid); });
    invalid = reflection;
    invalid["resources"][0]["variables"][1]["basic"] = "uint";
    reject([&] { surface_binding_layout(definition, invalid); });
    invalid = reflection;
    invalid["resources"][1]["dimension"] = "texture3d";
    reject([&] { surface_binding_layout(definition, invalid); });
    invalid = reflection;
    invalid["resources"][1]["name"] = "UnknownTexture";
    reject([&] { surface_binding_layout(definition, invalid); });
    // Compiler optimization may remove unused declarations; it cannot add an
    // unrecognized binding or change a declaration that survives reflection.
    reflection["resources"] = Json::array();
    check(surface_binding_layout(definition, reflection).parameter_bytes == 0,
          "Optimized-out surface fields require fake native resources");
    const auto fog_field = [](const char* name, unsigned offset) {
        return Json{
            {"name", name}, {"offset", offset}, {"basic", "float"}, {"class", "vector"},
            {"rows", 1},    {"columns", 4},     {"array_size", 0},  {"members", Json::array()}};
    };
    reflection["resources"] = Json::array(
        {{{"name", "ForgeDistanceFog"},
          {"kind", "constant_buffer"},
          {"array_size", 1},
          {"size", 32},
          {"variables",
           Json::array({fog_field("g_ForgeFogColor", 0), fog_field("g_ForgeFogRange", 16)})}}});
    (void)surface_binding_layout(definition, reflection);
    auto graph_definition = definition;
    graph_definition.physically_based = true;
    (void)surface_binding_layout(graph_definition, reflection);
    for (const auto& bad :
         {Json{{"kind", "texture_srv"}}, Json{{"size", 16}}, Json{{"array_size", 2}}}) {
        auto malformed = reflection;
        malformed["resources"][0].update(bad);
        reject([&] { (void)surface_binding_layout(definition, malformed); });
    }
    for (const auto& bad : {Json{{"offset", 4}}, Json{{"columns", 3}}, Json{{"name", "unknown"}},
                            Json{{"basic", "int"}}, Json{{"array_size", 1}}}) {
        auto malformed = reflection;
        malformed["resources"][0]["variables"][1].update(bad);
        reject([&] { (void)surface_binding_layout(definition, malformed); });
    }
    // Legacy cooks remain admissible without the optional new engine binding.
    reflection["resources"] = Json::array();
    (void)surface_binding_layout(definition, reflection);
    reject([&] { surface_shader_wrapper("quoted\".hlsl", "Shade", false); });
    reject([&] { surface_shader_wrapper("surface.hlsl", "Shade()", false); });

    // Sprite metadata is explicit; its image key owns the frame mapping while
    // other material textures retain their ordinary sampling settings.
    SurfaceShaderDefinition sprite;
    sprite.uv_sets = {0};
    sprite.sprite_image = "image";
    sprite.textures["image"].semantic = TextureSemantic::Color;
    sprite.textures["detail"].offset = {.5f, 0};
    sprite.labels["image"] = "Main image";
    const auto sprite_document = surface_definition_document(sprite);
    check(sprite_document.at("version") == 3 && sprite_document.at("domain") == "sprite" &&
              sprite_document.at("image") == "image" &&
              surface_definition(sprite_document) == sprite && document.at("version") == 1 &&
              !document.contains("domain"),
          "Sprite image domain/key did not round trip or changed legacy metadata");
    for (unsigned invalid = 0; invalid < 12; ++invalid) {
        auto wrong = sprite;
        auto& image = wrong.textures.at("image");
        switch (invalid) {
        case 0:
            wrong.sprite_image = "missing";
            break;
        case 1:
            wrong.sprite_image = "";
            break;
        case 2:
            wrong.physically_based = true;
            break;
        case 3:
            wrong.uv_sets = {17};
            break;
        case 4:
            image.semantic = TextureSemantic::Data;
            break;
        case 5:
            image.dimension = TextureDimension::Cube;
            break;
        case 6:
            image.offset[0] = .25f;
            break;
        case 7:
            image.sampler.min = TextureFilter::Nearest;
            break;
        case 8:
            wrong.uv_sets = {0, 17};
            break;
        case 9:
            image.sampler.lod_bias = 1;
            break;
        case 10:
            image.rotation = 1;
            break;
        case 11:
            wrong.labels["unknown"] = "Missing binding";
            break;
        }
        reject([&] { validate_surface_definition(wrong); });
    }
    auto sprite_values = surface_material_defaults(sprite);
    validate_surface_material(sprite_values, sprite);
    sprite_values.textures.at("image").scale[0] = 2;
    reject([&] { validate_surface_material(sprite_values, sprite); });
    reject([&] { surface_shader_header(sprite); });
    auto wrong_domain = sprite_document;
    wrong_domain["domain"] = "mesh";
    reject([&] { surface_definition(wrong_domain); });
    wrong_domain = sprite_document;
    wrong_domain["lighting"] = "metallic-roughness";
    reject([&] { surface_definition(wrong_domain); });
    sprite.parameters["tint"] = {MaterialParameterType::LinearColor4, {1, .5f, .25f, 1}};
    MeshVertexFetch sprite_fetch;
    sprite_fetch.uv_sets = {0};
    for (bool mips : {false, true})
        for (bool emulated : {false, true}) {
            const auto header = sprite_surface_shader_header(sprite, mips, emulated);
            check(header.find(mesh_shader_varyings(sprite_fetch, true, "ForgeSpriteInput", true)) !=
                          header.npos &&
                      header.find("float3 Normal:TEXCOORD") == header.npos &&
                      header.find("float2 ForgeUV0(ForgeSpriteInput input)") != header.npos &&
                      header.find("ForgeSample_image(ForgeSpriteInput input,float2 coordinate)") !=
                          header.npos &&
                      header.find(mips ? ".SampleGrad(" : ".SampleLevel(") != header.npos,
                  "Sprite shader input/sampling lost its shared geometry or frame contract");
            const auto wrapper = sprite_surface_shader_wrapper("surface.hlsl", "Shade", mips);
            check(wrapper.find(mips ? surface_sprite_mip_entry : surface_sprite_color_entry) !=
                          wrapper.npos &&
                      wrapper.find("(ForgeSpriteInput input):SV_Target0") != wrapper.npos &&
                      wrapper.find("ForgeApplyDistanceFog") != wrapper.npos &&
                      wrapper.find("#include \\") == wrapper.npos,
                  "Sprite color wrapper lost typed entry/fog or escaped its include path");
        }
    reject([&] { sprite_surface_shader_wrapper("bad\".hlsl", "Shade", false); });
    reject([&] { sprite_surface_shader_wrapper("surface.hlsl", "Shade()", true); });
    check(surface_shader_roles(sprite, 14).size() == 2,
          "Sprite surface gained Mesh-only entry roles");
    reject([&] { surface_shader_roles(sprite, 13); });
    for (const auto& [version, entries] :
         {std::pair{1u, 2u}, std::pair{2u, 2u}, std::pair{3u, 3u}, std::pair{4u, 5u},
          std::pair{5u, 7u}, std::pair{6u, 9u}, std::pair{7u, 17u}, std::pair{8u, 17u},
          std::pair{9u, 19u}, std::pair{13u, 19u}, std::pair{14u, 19u}}) {
        const auto roles = surface_shader_roles(definition, version);
        check(roles.size() == entries && roles[0] == ShaderEntryRole::SurfaceColor &&
                  roles[1] == ShaderEntryRole::SurfaceDepth,
              "Legacy Mesh surface entry capability changed");
    }
}
