#include "material_selection.hpp"
#include "pbr_material.hpp"
#include <forge/material_source.hpp>
#include <forge/render_texture.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid render texture or material binding was accepted");
}
} // namespace
int main() {
    try {
        const auto id = AssetId::generate();
        auto source = RenderTextureSource::create(id);
        source.document["vendor"] = {{"keep", "source extension"}};
        source.document["width"] = 2048;
        source.document["height"] = 1;
        const auto reopened = RenderTextureSource::parse(source.cook());
        check(reopened.document == source.document && reopened.asset() == id &&
                  reopened.compiled() == RenderTextureData{2048, 1},
              "Render texture cook lost identity, size or extensions");
        const auto retained = reopened.compiled();
        source.document["width"] = 16;
        check(retained.width == 2048 && source.compiled().width == 16,
              "Authored edit mutated retained output configuration");
        for (const Json& invalid : {Json(0), Json(-1), Json(2049), Json(1.0), Json(true),
                                    Json("32"), Json(std::numeric_limits<std::uint64_t>::max())})
            for (const auto* key : {"width", "height"}) {
                auto candidate = source;
                candidate.document[key] = invalid;
                rejects([&] { candidate.validate(); });
            }
        for (const auto* key : {"profile", "format", "version", "asset_id"}) {
            auto candidate = source;
            candidate.document[key] = nullptr;
            rejects([&] { candidate.validate(); });
        }
        rejects([&] { RenderTextureSource::create({}); });
        rejects([&] { validate_render_texture({2049, 32}); });
        auto invalid = source;
        invalid.document["vendor"] = std::numeric_limits<double>::infinity();
        rejects([&] { invalid.validate(); });
        invalid.document["vendor"] = std::string(render_texture_source_byte_limit, 'x');
        rejects([&] { invalid.validate(); });

        auto material = MaterialSource::create(AssetId::generate());
        material.document["version"] = 3;
        MaterialTextureSlot slot;
        slot.semantic = TextureSemantic::Color;
        MaterialData projection;
        projection.model = "forge.gltf.unlit.v1";
        projection.textures["baseColorTexture"] = slot;
        auto& binding = material.document["overrides"]["textures"]["baseColorTexture"];
        binding = {
            {"render_texture", id},
            {"slot", material_values_document(projection).at("textures").at("baseColorTexture")},
            {"vendor", {{"keep", true}}}};
        material.document["overrides"]["model"] = "forge.gltf.unlit.v1";
        auto resolved = resolve_material_source(material);
        check(resolved.textures.empty() && resolved.render_textures.at("baseColorTexture").id == id,
              "Render output was reinterpreted as an immutable source texture");
        MaterialResourceData data{resolved.values, resolved.textures, {}, resolved.render_textures};
        const auto files = asset_detail::encode_material_bundle(material.asset(), data);
        const auto decoded = asset_detail::decode_material_bundle(files, material.asset());
        check(decoded.values == data.values && decoded.textures.empty() &&
                  decoded.render_textures == data.render_textures && !decoded.surface,
              "Cooked material lost typed render output binding");
        auto duplicate = material;
        duplicate.document["overrides"]["textures"]["baseColorTexture"]["asset"] =
            AssetId::generate();
        rejects([&] { duplicate.validate(); });
        auto wrong_slot = data;
        wrong_slot.values.textures.at("baseColorTexture").semantic = TextureSemantic::Data;
        rejects([&] { validate_render_material(wrong_slot); });
        wrong_slot = data;
        wrong_slot.textures["baseColorTexture"] = {AssetId::generate()};
        rejects([&] { validate_render_material(wrong_slot); });

        auto instance = MaterialSource::create(AssetId::generate());
        instance.document["base"] = AssetRef<MaterialAsset>{material.asset()};
        check(resolve_material_source(instance, &resolved).render_textures ==
                  resolved.render_textures,
              "Inherited render texture selection was lost");
        instance.document["overrides"]["textures"]["baseColorTexture"] = nullptr;
        const auto cleared = resolve_material_source(instance, &resolved);
        check(cleared.render_textures.empty() && cleared.textures.empty() &&
                  cleared.values.textures.empty(),
              "Explicit null did not clear inherited output");
        instance.document["overrides"]["textures"]["baseColorTexture"] = binding;
        instance.document["version"] = 3;
        const auto static_id = AssetId::generate();
        auto& replacement = instance.document["overrides"]["textures"]["baseColorTexture"];
        replacement.erase("render_texture");
        replacement["asset"] = static_id;
        const auto replaced = resolve_material_source(instance, &resolved);
        check(replaced.render_textures.empty() &&
                  replaced.textures.at("baseColorTexture").id == static_id,
              "Static replacement retained a second binding for the same role");

        // An older source's unknown extension must remain opaque until explicit promotion.
        instance.document["version"] = 1;
        replacement["render_texture"] = "opaque vendor extension";
        instance.validate();
        check(resolve_material_source(instance, &resolved).textures.at("baseColorTexture").id ==
                  static_id,
              "Old source version reinterpreted an extension as a new binding");
        const auto opaque_before = instance.document;
        rejects([&] { instance.enable_render_textures(); });
        check(instance.document == opaque_before,
              "Rejected version promotion changed an opaque legacy extension");
        replacement.erase("render_texture");
        instance.enable_render_textures();
        check(instance.document.at("version") == 3 &&
                  resolve_material_source(instance, &resolved).textures.at("baseColorTexture").id ==
                      static_id,
              "Explicit promotion lost the existing static binding");
        std::cout
            << "Render output configuration and typed material source/bundle boundaries passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
