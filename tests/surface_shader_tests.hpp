#pragma once
#include "mesh_shader_input.hpp"
#include <cstring>
#include <forge/material_resource.hpp>
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
    const auto reflected_material = reflection.at("resources")[0];
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
    auto volume_field = fog_field("g_ForgeFogVolumes", 0);
    volume_field["array_size"] = 81;
    const Json volume_reflection{
        {"stage", "pixel"},
        {"resources", Json::array({{{"name", "ForgeFogVolumes"},
                                    {"kind", "constant_buffer"},
                                    {"array_size", 1},
                                    {"size", 1296},
                                    {"variables", Json::array({volume_field})}}})}};
    (void)surface_binding_layout(definition, volume_reflection, ShaderEntryRole::SurfaceColor, 16);
    reject([&] {
        (void)surface_binding_layout(definition, volume_reflection, ShaderEntryRole::SurfaceDepth,
                                     16);
    });
    reject([&] {
        (void)surface_binding_layout(definition, volume_reflection, ShaderEntryRole::SurfaceColor,
                                     15);
    });
    for (const auto& bad :
         {Json{{"size", 1280}}, Json{{"kind", "texture_srv"}}, Json{{"array_size", 2}}}) {
        auto wrong = volume_reflection;
        wrong["resources"][0].update(bad);
        reject([&] {
            (void)surface_binding_layout(definition, wrong, ShaderEntryRole::SurfaceColor, 16);
        });
    }
    for (const auto& bad : {Json{{"offset", 16}}, Json{{"basic", "uint"}}, Json{{"array_size", 80}},
                            Json{{"name", "unknown"}}}) {
        auto wrong = volume_reflection;
        wrong["resources"][0]["variables"][0].update(bad);
        reject([&] {
            (void)surface_binding_layout(definition, wrong, ShaderEntryRole::SurfaceColor, 16);
        });
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
    (void)surface_binding_layout(sprite, volume_reflection, ShaderEntryRole::SurfaceSpriteColor,
                                 16);
    reject([&] {
        (void)surface_binding_layout(sprite, volume_reflection, ShaderEntryRole::SurfaceSpriteColor,
                                     14);
    });
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
    auto ui = definition;
    ui.ui_effect = true;
    ui.uv_sets = {0};
    ui.textures.clear();
    ui.labels = {{"amount", "Amount"}};
    const auto ui_document = surface_definition_document(ui);
    check(ui_document.at("version") == 4 && ui_document.at("domain") == "ui" &&
              surface_definition(ui_document) == ui,
          "UI surface domain/labels did not round trip");
    auto ui_material = surface_material_defaults(ui);
    validate_surface_material(ui_material, ui);
    check(ui_material.alpha == MaterialAlpha::Blend && !ui_material.depth_test &&
              !ui_material.depth_write && ui_material.double_sided,
          "UI effect default render state differs from native composition");
    ui_material.depth_test = true;
    reject([&] { validate_surface_material(ui_material, ui); });
    ui_material = surface_material_defaults(ui);
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto invalid_ui = ui;
        if (mode == 0)
            invalid_ui.physically_based = true;
        else if (mode == 1)
            invalid_ui.uv_sets.clear();
        else if (mode == 2)
            invalid_ui.textures = definition.textures;
        else
            invalid_ui.sprite_image = "color";
        reject([&] { validate_surface_definition(invalid_ui); });
    }
    check(surface_shader_roles(ui, 17).size() == 1 &&
              surface_shader_roles(ui, 17)[0] == ShaderEntryRole::SurfaceUiColor &&
              shader_entry_role("surface_ui_color") == ShaderEntryRole::SurfaceUiColor,
          "UI surface dispatch gained geometry/lighting stages");
    reject([&] { surface_shader_roles(ui, 16); });
    reject([&] { surface_shader_header(ui); });
    const auto ui_header = ui_surface_shader_header(ui);
    const auto ui_wrapper = ui_surface_shader_wrapper("effects/pulse.hlsl", "Pulse");
    check(ui_header.find("ForgeUiInput") != ui_header.npos &&
              ui_header.find("ForgeParameter_amount") != ui_header.npos &&
              ui_header.find("Texture2D") == ui_header.npos &&
              ui_wrapper.find("value.a=saturate(value.a)*saturate(color.a)") != ui_wrapper.npos &&
              ui_wrapper.find("LinearToSRGB") != ui_wrapper.npos &&
              ui_wrapper.find("value.rgb*=value.a") != ui_wrapper.npos,
          "UI shader interface lost numeric defaults, native opacity or target color transfer");
    reject([&] { ui_surface_shader_wrapper("bad\".hlsl", "Pulse"); });
    reject([&] { ui_surface_shader_wrapper("pulse.hlsl", "Pulse()"); });
    auto ui_field = field("element", 0, 4);
    ui_field["name"] = "g_ForgeUiElement";
    Json ui_reflection{{"stage", "pixel"},
                       {"threads", Json{0, 0, 0}},
                       {"resources", Json::array({reflected_material,
                                                  {{"name", "ForgeUiState"},
                                                   {"kind", "constant_buffer"},
                                                   {"array_size", 1},
                                                   {"size", 16},
                                                   {"variables", Json::array({ui_field})}}})}};
    unsigned register_index = 0;
    for (auto& resource : ui_reflection["resources"]) {
        resource["register"] = register_index++;
        resource["space"] = 0;
        resource["dimension"] = "unknown";
    }
    const auto ui_layout =
        surface_binding_layout(ui, ui_reflection, ShaderEntryRole::SurfaceUiColor, 17);
    check(ui_layout.settings && ui_layout.parameter_bytes == 32 &&
              surface_parameter_bytes(ui_material, ui_layout) == packed,
          "UI Material values did not reuse reflected parameter packing");
    reject([&] { surface_binding_layout(ui, ui_reflection, ShaderEntryRole::SurfaceColor, 17); });
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto invalid_reflection = ui_reflection;
        auto& state = invalid_reflection["resources"][1];
        if (mode == 0)
            state["name"] = "ForgeSurfaceState";
        else if (mode == 1)
            state["variables"][0]["offset"] = 4;
        else if (mode == 2)
            state["size"] = 32;
        else
            invalid_reflection["resources"].push_back(state);
        reject([&] {
            surface_binding_layout(ui, invalid_reflection, ShaderEntryRole::SurfaceUiColor, 17);
        });
    }
    // Structural envelope proof, not native bytecode compilation or GPU execution.
    ShaderData ui_program;
    ui_program.build_key = std::string(64, 'a');
    ui_program.compiler_digest = std::string(64, 'b');
    ui_program.surface = ui;
    ui_program.surface_wrapper_version = 17;
    ui_program.stages.push_back({ShaderStage::Pixel,
                                 "ForgeSurfaceUiColor",
                                 {std::byte{1}},
                                 ui_reflection,
                                 ShaderEntryRole::SurfaceUiColor});
    const auto ui_restored = decode_shader(encode_shader(ui_program));
    check(ui_restored.surface == ui && ui_restored.surface_wrapper_version == 17 &&
              ui_restored.stages.size() == 1 && ui_restored.stages[0].reflection == ui_reflection,
          "Cooked UI domain/role/reflection did not round trip");
    auto textured_ui = ui;
    textured_ui.textures["image"].semantic = TextureSemantic::Color;
    textured_ui.textures["image"].scale = {-2, 0};
    textured_ui.textures["image"].offset = {3, 4};
    const auto textured_document = surface_definition_document(textured_ui);
    check(textured_document.at("version") == 5 &&
              surface_definition(textured_document) == textured_ui,
          "Textured UI interface lost its explicit capability or signed/zero UV intent");
    auto invalid_document = textured_document;
    invalid_document["version"] = 4;
    reject([&] { surface_definition(invalid_document); });
    invalid_document = ui_document;
    invalid_document["version"] = 5;
    reject([&] { surface_definition(invalid_document); });
    auto invalid_textured_ui = textured_ui;
    invalid_textured_ui.textures.at("image").dimension = TextureDimension::D2Array;
    reject([&] { validate_surface_definition(invalid_textured_ui); });
    reject([&] { surface_shader_roles(textured_ui, 17); });
    check(surface_shader_roles(textured_ui, 18).size() == 1,
          "Textured UI gained non-UI geometry stages");
    const auto textured_header = ui_surface_shader_header(textured_ui);
    check(
        textured_header.find("Texture2D<float4> g_SurfaceTexture_image") != textured_header.npos &&
            textured_header.find("ForgeSample_image(ForgeUiInput input)") != textured_header.npos &&
            textured_header.find("ForgeSample_image(ForgeUV0(input))") != textured_header.npos &&
            textured_header.find("input.UV[") == textured_header.npos &&
            textured_header.find("ForgeSurfaceInput") == textured_header.npos,
        "UI Texture helpers lost native UV input or introduced Mesh geometry");
    auto textured_reflection = ui_reflection;
    auto uv_field = field("unused", 0, 4);
    uv_field["name"] = "g_SurfaceUvRows";
    uv_field["array_size"] = 2;
    textured_reflection["resources"].push_back({{"name", "ForgeSurfaceUV"},
                                                {"kind", "constant_buffer"},
                                                {"array_size", 1},
                                                {"size", 32},
                                                {"variables", Json::array({uv_field})},
                                                {"register", 2},
                                                {"space", 0},
                                                {"dimension", "unknown"}});
    textured_reflection["resources"].push_back({{"name", "g_SurfaceTexture_image"},
                                                {"kind", "texture_srv"},
                                                {"array_size", 1},
                                                {"dimension", "texture2d"},
                                                {"register", 0},
                                                {"space", 0}});
    textured_reflection["resources"].push_back({{"name", "g_SurfaceSamplers"},
                                                {"kind", "sampler"},
                                                {"array_size", 1},
                                                {"register", 0},
                                                {"space", 0},
                                                {"dimension", "unknown"}});
    const auto textured_layout = surface_binding_layout(textured_ui, textured_reflection,
                                                        ShaderEntryRole::SurfaceUiColor, 18);
    check(textured_layout.uv_bytes == 32 && textured_layout.sampler_count == 1 &&
              textured_layout.textures == std::vector<std::string>{"image"} &&
              surface_uv_rows(surface_material_defaults(textured_ui), textured_ui) ==
                  std::vector<std::array<float, 4>>{{-2, 0, 3, 0}, {0, 0, 4, 0}},
          "UI Texture reflection or shared UV packing differs from Material intent");
    reject([&] {
        surface_binding_layout(textured_ui, textured_reflection, ShaderEntryRole::SurfaceUiColor,
                               17);
    });
    for (unsigned mode = 0; mode < 5; ++mode) {
        auto malformed = textured_reflection;
        if (mode == 0)
            malformed["resources"][3]["dimension"] = "texture3d";
        else if (mode == 1)
            malformed["resources"][3]["name"] = "g_SurfaceTexture_undeclared";
        else if (mode == 2)
            malformed["resources"][4]["array_size"] = 2;
        else if (mode == 3)
            malformed["resources"][2]["variables"][0]["offset"] = 16;
        else
            malformed["resources"][2]["size"] = 16;
        reject([&] {
            surface_binding_layout(textured_ui, malformed, ShaderEntryRole::SurfaceUiColor, 18);
        });
    }
    auto textured_program = ui_program;
    textured_program.surface = textured_ui;
    textured_program.surface_wrapper_version = 18;
    textured_program.stages[0].reflection = textured_reflection;
    check(decode_shader(encode_shader(textured_program)).surface == textured_ui,
          "Cooked UI Texture capability/reflection did not round trip");
    MaterialResourceData textured_material;
    textured_material.values = surface_material_defaults(textured_ui);
    textured_material.surface =
        MaterialShaderSnapshot{{AssetId::generate()}, std::string(64, 'c'), textured_program};
    reject([&] { validate_ui_effect_material(textured_material); });
    textured_material.textures["image"] = {AssetId::generate()};
    validate_ui_effect_material(textured_material);
    auto invalid_resource = textured_material;
    invalid_resource.textures.clear();
    invalid_resource.render_textures["image"] = {AssetId::generate()};
    reject([&] { validate_render_material(invalid_resource); });
    reject([&] { validate_ui_effect_material(invalid_resource); });
}
