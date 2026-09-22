#pragma once
#include "pbr_material.hpp"
#include <forge/material_source.hpp>
inline void test_material_source() {
    using namespace forge;
    using Json = nlohmann::json;
    const auto check = [](bool ok) {
        if (!ok)
            throw std::runtime_error("Material source regression");
    };
    const auto rejects = [](auto&& fn) {
        try {
            fn();
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Invalid material source admitted");
    };
    const auto parse = [](const Json& j) {
        const auto bytes = j.dump();
        return MaterialSource::parse(std::as_bytes(std::span(bytes)));
    };
    auto base = MaterialSource::create(AssetId::generate());
    base.document["plugin.extension"] = {{"future", {1, "opaque", true}}};
    base.document["overrides"]["parameters"]["roughnessFactor"] = {
        {"type", unsigned(MaterialParameterType::Scalar)}, {"value", {.4f}}};
    const auto raw_base = base.document;
    auto resolved_base = resolve_material_source(base);
    check(parse(base.document).document == raw_base && base.document == raw_base);
    auto instance = MaterialSource::create(AssetId::generate());
    instance.document["base"] = AssetRef<MaterialAsset>{base.asset()};
    auto& overrides = instance.document["overrides"];
    overrides["parameters"]["roughnessFactor"] =
        base.document["overrides"]["parameters"]["roughnessFactor"];
    const auto raw_instance = instance.document;
    auto same = resolve_material_source(instance, &resolved_base);
    check(same.values.parameters.at("roughnessFactor").value[0] == .4f);
    check(instance.document == raw_instance); // Equal values do not erase intent.
    resolved_base.values.parameters.at("roughnessFactor").value[0] = .8f;
    check(resolve_material_source(instance, &resolved_base)
              .values.parameters.at("roughnessFactor")
              .value[0] == .4f);
    overrides["parameters"].erase("roughnessFactor"); // Revert.
    check(resolve_material_source(instance, &resolved_base)
              .values.parameters.at("roughnessFactor")
              .value[0] == .8f);
    overrides["parameters"]["roughnessFactor"] = nullptr; // Explicit reset to model default.
    const auto reset = resolve_material_source(instance, &resolved_base);
    check(!reset.values.parameters.contains("roughnessFactor") &&
          prepare_pbr_material(reset.values).values.parameters.at("roughnessFactor").value[0] == 1);
    MaterialData texture_values;
    texture_values.model = resolved_base.values.model;
    texture_values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    const auto texture_id = AssetId::generate();
    base.document["overrides"]["textures"]["baseColorTexture"] = {
        {"asset", texture_id},
        {"slot", material_values_document(texture_values).at("textures").at("baseColorTexture")}};
    resolved_base = resolve_material_source(base);
    check(resolve_material_source(instance, &resolved_base).textures.at("baseColorTexture").id ==
          texture_id);
    overrides["textures"]["baseColorTexture"] = nullptr;
    const auto cleared = resolve_material_source(instance, &resolved_base);
    check(cleared.textures.empty() && cleared.values.textures.empty());
    overrides["textures"].erase("baseColorTexture");
    check(resolve_material_source(instance, &resolved_base).textures.at("baseColorTexture").id ==
          texture_id);
    rejects([&] { resolve_material_source(instance); });
    rejects([&] { resolve_material_source(base, &resolved_base); });
    auto bad = base;
    bad.document["base"] = bad.asset();
    rejects([&] { bad.validate(); });
    for (const auto value : {Json(3), Json(1.0), Json("1")}) {
        bad = base;
        bad.document["version"] = value;
        rejects([&] { parse(bad.document); });
    }
    bad = base;
    bad.document["overrides"]["parameters"]["unknown"] = nullptr;
    rejects([&] { resolve_material_source(bad); });
    bad = base;
    bad.document["overrides"]["textures"]["unknown"] = nullptr;
    rejects([&] { resolve_material_source(bad); });
    bad = base;
    bad.document["overrides"]["parameters"]["roughnessFactor"]["value"] = {-1};
    check(parse(bad.document).document == bad.document); // Unfinished model value, valid shape.
    rejects([&] { resolve_material_source(bad); });
    for (const auto malformed : {Json("wrong"), Json::array({"wrong"}), Json::array({1, 2})}) {
        bad = base;
        bad.document["overrides"]["parameters"]["roughnessFactor"]["value"] = malformed;
        rejects([&] { parse(bad.document); });
    }
    bad = base;
    bad.document["overrides"]["textures"]["baseColorTexture"]["slot"]["sampler"] = false;
    rejects([&] { parse(bad.document); });
    bad = base;
    bad.document["overrides"]["textures"]["baseColorTexture"]["asset"] = "invalid";
    rejects([&] { resolve_material_source(bad); });
    bad = base;
    bad.document["future"] = std::numeric_limits<double>::infinity();
    rejects([&] { bad.validate(); });
    const std::string duplicate = "{\"kind\":\"forge.material\",\"kind\":\"forge.material\"}";
    rejects([&] { MaterialSource::parse(std::as_bytes(std::span(duplicate))); });
    auto blend = MaterialSource::create(AssetId::generate());
    blend.document["overrides"]["alpha"] = unsigned(MaterialAlpha::Blend);
    check(!resolve_material_source(blend).values.depth_write);
    blend.document["overrides"]["depth_write"] = true;
    check(resolve_material_source(blend).values.depth_write);
    check(material_values_from_document(material_values_document(resolved_base.values)) ==
          resolved_base.values);
    SurfaceShaderDefinition surface;
    surface.parameters["tint"] = {MaterialParameterType::LinearColor4, {.1f, .2f, .3f, 1}};
    auto custom = MaterialSource::create(AssetId::generate());
    const AssetRef<ShaderAsset> shader{AssetId::generate()};
    custom.document["overrides"]["shader"] = shader;
    rejects([&] { custom.validate(); }); // Older readers must reject the new intent.
    custom.document["version"] = 2;
    check(parse(custom.document).document == custom.document);
    rejects([&] { resolve_material_source(custom); });
    auto custom_base = resolve_material_source(custom, nullptr, &surface);
    check(custom_base.shader == shader && custom_base.values.model == surface_material_model &&
          custom_base.values.parameters == surface.parameters);
    auto custom_child = MaterialSource::create(AssetId::generate());
    custom_child.document["base"] = custom.asset();
    const auto original = custom_child.document;
    check(resolve_material_source(custom_child, &custom_base, &surface).shader == shader &&
          custom_child.document == original);
    auto& child_overrides = custom_child.document["overrides"];
    child_overrides["parameters"]["tint"] =
        material_values_document(custom_base.values).at("parameters").at("tint");
    custom_base.values.parameters.at("tint").value[0] = .8f;
    check(resolve_material_source(custom_child, &custom_base, &surface)
              .values.parameters.at("tint")
              .value[0] == .1f);
    child_overrides["parameters"].erase("tint");
    check(resolve_material_source(custom_child, &custom_base, &surface)
              .values.parameters.at("tint")
              .value[0] == .8f);
    child_overrides["parameters"]["tint"] = nullptr;
    check(resolve_material_source(custom_child, &custom_base, &surface)
              .values.parameters.at("tint")
              .value[0] == .1f);
    child_overrides["parameters"]["unknown"] = nullptr;
    rejects([&] { resolve_material_source(custom_child, &custom_base, &surface); });
    child_overrides["parameters"].erase("unknown");
    custom_child.document["version"] = 2;
    child_overrides["shader"] = nullptr;
    check(!material_shader_reference(custom_child, &custom_base));
    rejects([&] { resolve_material_source(custom_child, &custom_base, &surface); });
    // Clearing a custom model does not silently discard its inherited values.
    rejects([&] { resolve_material_source(custom_child, &custom_base); });
}
