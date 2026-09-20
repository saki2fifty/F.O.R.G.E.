#pragma once
#include "pbr_material.hpp"
#include <cmath>
#include <limits>
inline void test_pbr_material_profile() {
    using namespace forge;
    using Type = MaterialParameterType;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto rejects = [&](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid built-in PBR material was accepted");
    };
    MaterialData source;
    source.model = "forge.gltf.metallic-roughness.v1";
    auto prepared = prepare_pbr_material(source);
    check(source.parameters.empty() && prepared.layout.parameters.empty() &&
              prepared.values.parameters.at("baseColorFactor").value ==
                  std::array<float, 4>{1, 1, 1, 1} &&
              !prepared.values.parameters.contains("attenuationDistance") &&
              std::abs(prepared.dielectric_f0 - .04f) < 1e-7,
          "PBR defaults mutated source or fabricated finite attenuation distance");
    source.parameters["normalScale"] = {Type::Scalar, {-2}};
    source.parameters["clearcoatNormalScale"] = {Type::Scalar, {-3}};
    source.parameters["specularColorFactor"] = {Type::LinearColor3, {2, 3, 4}};
    source.parameters["iridescenceThicknessMinimum"] = {Type::Scalar, {400}};
    source.parameters["iridescenceThicknessMaximum"] = {Type::Scalar, {100}};
    source.parameters["ior"] = {Type::Scalar, {0}};
    source.alpha = MaterialAlpha::Mask;
    source.alpha_cutoff = 2;
    MaterialTextureSlot slot;
    slot.semantic = TextureSemantic::Normal;
    slot.uv_set = 19;
    slot.scale = {-2, 0};
    slot.offset = {3, -4};
    slot.rotation = .7f;
    source.textures["normalTexture"] = slot;
    prepared = prepare_pbr_material(source);
    check(prepared.dielectric_f0 == 1 && prepared.values.textures.at("normalTexture") == slot &&
              prepared.values.alpha_cutoff == 2 && prepared.layout.parameters.size() == 6,
          "Valid signed normal/UV, IOR or reversed-thickness content was altered");
    for (const float value : {1.f, 1.5f, 2.f, std::numeric_limits<float>::max()}) {
        source.parameters["ior"].value[0] = value;
        prepared = prepare_pbr_material(source);
        const double ratio = (double(value) - 1) / (double(value) + 1);
        check(std::abs(prepared.dielectric_f0 - ratio * ratio) < 1e-7,
              "IOR reflectance calculation overflowed or used fixed 1.5");
    }
    const auto valid = source;
    const auto invalid = [&](auto mutate) {
        auto candidate = valid;
        mutate(candidate);
        rejects([&] { prepare_pbr_material(candidate); });
        check(source == valid, "Failed candidate changed previous material");
    };
    invalid([](auto& m) { m.parameters["unexpected"] = {}; });
    invalid([](auto& m) { m.parameters["ior"].value[0] = .5f; });
    invalid([](auto& m) { m.parameters["ior"].type = Type::Vector4; });
    invalid([](auto& m) { m.parameters["metallicFactor"] = {Type::Scalar, {2}}; });
    invalid([](auto& m) { m.parameters["attenuationDistance"] = {Type::Scalar, {0}}; });
    invalid([](auto& m) { m.parameters["baseColorFactor"] = {Type::LinearColor4, {1, 1, 2, 1}}; });
    invalid([](auto& m) { m.textures["normalTexture"].semantic = TextureSemantic::Color; });
    invalid([](auto& m) { m.textures["normalTexture"].dimension = TextureDimension::Cube; });
    invalid([](auto& m) { m.textures["normalTexture"].sampler.compare = TextureCompare::Less; });
    invalid([](auto& m) { m.textures["customTexture"] = {}; });
    invalid([](auto& m) { m.model = "custom.unknown"; });
    MaterialData legacy;
    legacy.model = "forge.gltf.specular-glossiness.v1";
    legacy.parameters["specularFactor"] = {Type::LinearColor3, {.2f, .3f, .4f}};
    legacy.textures["specularGlossinessTexture"].semantic = TextureSemantic::Color;
    prepared = prepare_pbr_material(legacy);
    check(prepared.workflow == PbrWorkflow::SpecularGlossiness &&
              prepared.values.parameters.contains("glossinessFactor") &&
              !prepared.values.parameters.contains("roughnessFactor"),
          "Legacy glossiness or color-space contract lost");
    legacy.parameters["specularFactor"].type = Type::Vector3;
    rejects([&] { prepare_pbr_material(legacy); });
    MaterialData unlit;
    unlit.model = "forge.gltf.unlit.v1";
    unlit.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    check(prepare_pbr_material(unlit).workflow == PbrWorkflow::Unlit, "Unlit model rejected");
    unlit.parameters["clearcoatFactor"] = {Type::Scalar, {.5f}};
    rejects([&] { prepare_pbr_material(unlit); });
    unlit.parameters.clear();
    unlit.textures["normalTexture"].semantic = TextureSemantic::Normal;
    rejects([&] { prepare_pbr_material(unlit); });
}
