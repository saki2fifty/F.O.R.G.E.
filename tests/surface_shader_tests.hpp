#pragma once
#include <cstring>
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
    reject([&] { surface_shader_wrapper("quoted\".hlsl", "Shade", false); });
    reject([&] { surface_shader_wrapper("surface.hlsl", "Shade()", false); });
}
