#include "bounded_json.hpp"
#include "pbr_material.hpp"
#include <cmath>
#include <forge/material_source.hpp>
namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void object(const Json& j, std::size_t maximum) {
    require(j.is_object() && j.size() <= maximum, "Material source field count exceeds limits");
}
} // namespace
AssetId MaterialSource::asset() const { return document.at("asset_id").get<AssetId>(); }
std::optional<AssetRef<MaterialAsset>> MaterialSource::base() const {
    const auto found = document.find("base");
    if (found == document.end() || found->is_null())
        return {};
    return found->get<AssetRef<MaterialAsset>>();
}
MaterialSource MaterialSource::parse(std::span<const std::byte> bytes) {
    MaterialSource result{
        asset_detail::parse_bounded_json(bytes, material_source_byte_limit, 32768, 32)};
    result.validate();
    return result;
}
MaterialSource MaterialSource::create(AssetId id) {
    MaterialSource result{{{"kind", "forge.material"},
                           {"version", 1},
                           {"asset_id", id},
                           {"overrides", Json::object()}}};
    result.validate();
    return result;
}
void MaterialSource::validate() const {
    object(document, 64);
    require(document.at("kind") == "forge.material" && document.at("version").is_number_integer() &&
                document.at("version") == 1,
            "Unsupported material source kind/version");
    require(bool(asset()), "Material source requires a persistent AssetId");
    if (const auto parent = base())
        require(bool(parent->id) && parent->id != asset(), "Material cannot inherit itself");
    const auto& overrides = document.at("overrides");
    object(overrides, 64);
    if (overrides.contains("parameters"))
        object(overrides.at("parameters"), 256);
    if (overrides.contains("textures"))
        object(overrides.at("textures"), 64);
    // Validate typed structure independently of a base/model's numeric rules.
    // Drafts may contain an unfinished roughness value, but never a string where
    // a numeric vector or sampler is required. Reuse the cooked value contract.
    MaterialData shape;
    shape.model = "forge.gltf.metallic-roughness.v1";
    auto projection = material_values_document(shape);
    for (const auto* key :
         {"model", "alpha", "alpha_cutoff", "double_sided", "depth_test", "depth_write"})
        if (overrides.contains(key))
            projection[key] = overrides.at(key);
    if (overrides.contains("parameters"))
        for (const auto& [name, value] : overrides.at("parameters").items())
            if (!value.is_null())
                projection["parameters"][name] = value;
    if (overrides.contains("textures"))
        for (const auto& [name, value] : overrides.at("textures").items())
            if (!value.is_null()) {
                object(value, 32);
                require(bool(value.at("asset").get<AssetRef<TextureAsset>>().id),
                        "Material texture override requires an AssetId");
                projection["textures"][name] = value.at("slot");
            }
    (void)material_values_from_document(projection);
    // In-memory drafts use the same bounded admission as files. Round-tripping
    // here also rejects nonfinite JSON numbers rather than serializing them null.
    std::size_t count = 0;
    const auto walk = [&](auto&& self, const Json& j, unsigned depth) -> void {
        require(depth <= 32 && ++count <= 32768, "Material source exceeds structural limits");
        if (j.is_number_float())
            require(std::isfinite(j.get<double>()), "Nonfinite material source number");
        if (j.is_structured())
            for (const auto& v : j)
                self(self, v, depth + 1);
    };
    walk(walk, document, 0);
    const auto encoded = document.dump();
    (void)asset_detail::parse_bounded_json(std::as_bytes(std::span(encoded)),
                                           material_source_byte_limit, 32768, 32);
}
ResolvedMaterialSource resolve_material_source(const MaterialSource& source,
                                               const ResolvedMaterialSource* base) {
    source.validate();
    require(bool(source.base()) == bool(base), "Material base revision is missing or unexpected");
    ResolvedMaterialSource result;
    result.values.model = "forge.gltf.metallic-roughness.v1";
    if (base) {
        validate_material_bindings(base->values, base->textures);
        (void)prepare_pbr_material(base->values);
        result = *base;
    }
    auto values = material_values_document(result.values);
    const auto& overrides = source.document.at("overrides");
    for (const auto* key :
         {"model", "alpha", "alpha_cutoff", "double_sided", "depth_test", "depth_write"})
        if (overrides.contains(key))
            values[key] = overrides.at(key);
    if (!base && values.at("alpha") == unsigned(MaterialAlpha::Blend) &&
        !overrides.contains("depth_write"))
        values["depth_write"] = false;
    MaterialData default_values;
    default_values.model = values.at("model").get<std::string>();
    const auto defaults = prepare_pbr_material(default_values);
    if (overrides.contains("parameters"))
        for (const auto& [name, parameter] : overrides.at("parameters").items()) {
            require(defaults.values.parameters.contains(name) ||
                        (defaults.workflow == PbrWorkflow::MetallicRoughness &&
                         name == "attenuationDistance"),
                    "Unknown parameter override for material model");
            if (parameter.is_null())
                values["parameters"].erase(name);
            else
                values["parameters"][name] = parameter;
        }
    if (overrides.contains("textures"))
        for (const auto& [name, texture] : overrides.at("textures").items()) {
            require(defaults.layout.textures.contains(name),
                    "Unknown texture override for material model");
            if (texture.is_null()) {
                values["textures"].erase(name);
                result.textures.erase(name);
            } else {
                object(texture, 32);
                values["textures"][name] = texture.at("slot");
                result.textures[name] = texture.at("asset").get<AssetRef<TextureAsset>>();
            }
        }
    result.values = material_values_from_document(values);
    validate_material_bindings(result.values, result.textures);
    (void)prepare_pbr_material(result.values);
    return result;
}
} // namespace forge
