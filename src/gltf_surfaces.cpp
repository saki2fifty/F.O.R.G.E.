#include "gltf_surfaces.hpp"
#include "gltf_native.hpp"
#include "gltf_validation.hpp"
#include <GLTFLoader.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tiny_gltf.h>

namespace forge::asset_detail {
namespace {
using namespace gltf_detail;
constexpr double max_float = std::numeric_limits<float>::max();
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("glTF surface: " + message);
}
const Json& object(const Json& value) {
    require(value.is_object(), "expected object");
    return value;
}
const Json& optional_object(const Json& parent, const char* key) {
    static const Json empty = Json::object();
    return parent.contains(key) ? object(parent.at(key)) : empty;
}
float number(const Json& value, double low = -max_float, double high = max_float,
             const char* label = "parameter") {
    require(value.is_number(), std::string(label) + " must be numeric");
    const double d = value.get<double>();
    require(std::isfinite(d) && d >= low && d <= high,
            std::string(label) + " outside supported numeric range");
    const float f = static_cast<float>(d);
    require(std::isfinite(f) && (d == 0 || f != 0),
            std::string(label) + " is not representable as float");
    return f;
}
void scalar(const Json& o, const char* key, double low = 0, double high = 1) {
    if (o.contains(key))
        (void)number(o.at(key), low, high, key);
}
void vector(const Json& o, const char* key, std::size_t width, double low = 0, double high = 1) {
    if (!o.contains(key))
        return;
    const auto& values = o.at(key);
    require(values.is_array() && values.size() == width,
            std::string(key) + " has wrong vector shape");
    for (const auto& v : values)
        (void)number(v, low, high, key);
}
bool declared(const GltfSourceBundle& s, const char* name) {
    const auto& used = array(s.document, "extensionsUsed", 256);
    return std::find(used.begin(), used.end(), name) != used.end();
}
const Json* declared_extension(const GltfSourceBundle& s, const Json& value, const char* name) {
    const auto* ext = extension(value, name);
    if (ext)
        require(declared(s, name), std::string("undeclared extension ") + name);
    return ext;
}
TextureWrap wrap(std::size_t value) {
    switch (value) {
    case 10497:
        return TextureWrap::Repeat;
    case 33648:
        return TextureWrap::MirroredRepeat;
    case 33071:
        return TextureWrap::ClampEdge;
    default:
        throw std::runtime_error("glTF surface: unsupported sampler wrap mode");
    }
}
SamplerState sampler(const Json& s) {
    object(s);
    SamplerState result;
    result.u = wrap(size_or(s, "wrapS", 10497));
    result.v = wrap(size_or(s, "wrapT", 10497));
    const auto mag = size_or(s, "magFilter", 9729);
    require(mag == 9728 || mag == 9729, "unsupported magnification filter");
    result.mag = mag == 9728 ? TextureFilter::Nearest : TextureFilter::Linear;
    // glTF leaves an unspecified filter to the implementation. FORGE chooses
    // linear magnification and trilinear minification, respecting explicit values.
    const auto min = size_or(s, "minFilter", 9987);
    require(min == 9728 || min == 9729 || (min >= 9984 && min <= 9987),
            "unsupported minification filter");
    result.min = (min == 9728 || min == 9984 || min == 9986) ? TextureFilter::Nearest
                                                             : TextureFilter::Linear;
    result.mip = (min == 9984 || min == 9985) ? TextureFilter::Nearest : TextureFilter::Linear;
    if (min == 9728 || min == 9729)
        result.max_lod = 0;
    validate_sampler(result);
    return result;
}
std::size_t texture_image(const GltfSourceBundle& s, const Json& texture, std::string& encoding) {
    const auto& images = array(s.document, "images", 1000000);
    std::optional<std::size_t> selected;
    // Both alternatives may exist. Prefer the admitted GPU-oriented container;
    // a failed selected decoder is not silently replaced with a stale fallback.
    for (const auto name : {"KHR_texture_basisu", "EXT_texture_webp"}) {
        if (const auto* ext = declared_extension(s, texture, name)) {
            require(ext->contains("source"), std::string(name) + " has no image source");
            if (!texture.contains("source")) {
                const auto& required = array(s.document, "extensionsRequired", 256);
                require(std::find(required.begin(), required.end(), name) != required.end(),
                        std::string(name) + " without fallback must be required");
            }
            const auto index = size_value(ext->at("source"));
            require(index < images.size(), "extension image index is invalid");
            if (!selected) {
                selected = index;
                encoding = name;
            }
        }
    }
    if (texture.contains("source")) {
        const auto index = size_value(texture.at("source"));
        require(index < images.size(), "image index is invalid");
        if (!selected)
            selected = index;
    }
    require(selected.has_value(), "used texture has no supported image source");
    require(*selected < s.images.size(), "selected image was not captured");
    const auto& image = s.images[*selected];
    if (encoding == "KHR_texture_basisu")
        require(image.mime_type.empty() || image.mime_type == "image/ktx2",
                "Basis image MIME mismatch");
    if (encoding == "EXT_texture_webp")
        require(image.mime_type.empty() || image.mime_type == "image/webp",
                "WebP image MIME mismatch");
    return *selected;
}
GltfTextureBinding binding(const GltfSourceBundle& s, const Json& info, std::string role,
                           TextureSemantic semantic) {
    object(info);
    const auto& textures = array(s.document, "textures", 1000000);
    const auto& samplers = array(s.document, "samplers", 1000000);
    GltfTextureBinding result;
    result.role = std::move(role);
    result.semantic = semantic;
    result.texture = size_value(info.at("index"));
    require(result.texture < textures.size(), "texture binding index is invalid");
    const auto& texture = object(textures[result.texture]);
    result.image = texture_image(s, texture, result.image_extension);
    if (texture.contains("sampler")) {
        const auto index = size_value(texture.at("sampler"));
        require(index < samplers.size(), "sampler index is invalid");
        result.sampler = sampler(samplers[index]);
    }
    auto uv = size_or(info, "texCoord", 0);
    if (const auto* transform = declared_extension(s, info, "KHR_texture_transform")) {
        vector(*transform, "offset", 2, -max_float, max_float);
        vector(*transform, "scale", 2, -max_float, max_float);
        scalar(*transform, "rotation", -max_float, max_float);
        if (transform->contains("offset"))
            result.offset = transform->at("offset").get<std::array<float, 2>>();
        if (transform->contains("scale"))
            result.scale = transform->at("scale").get<std::array<float, 2>>();
        if (transform->contains("rotation"))
            result.rotation = number(transform->at("rotation"));
        uv = size_or(*transform, "texCoord", uv);
    }
    // Mesh source admission permits arbitrary set indices (with consecutive sets
    // and bounded streams); do not narrow this to Diligent's packed selector bits.
    require(uv <= UINT32_MAX, "UV set index exceeds supported representation");
    result.uv_set = static_cast<unsigned>(uv);
    if (semantic == TextureSemantic::Normal)
        scalar(info, "scale", -max_float, max_float);
    if (result.role == "occlusionTexture")
        scalar(info, "strength");
    return result;
}
std::vector<GltfTextureBinding> material(const GltfSourceBundle& s, const Json& m) {
    object(m);
    if (m.contains("name"))
        require(m.at("name").is_string() &&
                    m.at("name").get_ref<const std::string&>().size() <= 4096,
                "material name is invalid");
    if (m.contains("doubleSided"))
        require(m.at("doubleSided").is_boolean(), "doubleSided must be boolean");
    const auto alpha = m.value("alphaMode", std::string("OPAQUE"));
    require(alpha == "OPAQUE" || alpha == "MASK" || alpha == "BLEND", "unsupported alpha mode");
    scalar(m, "alphaCutoff", 0, max_float);
    vector(m, "emissiveFactor", 3);
    const auto& pbr = optional_object(m, "pbrMetallicRoughness");
    vector(pbr, "baseColorFactor", 4);
    scalar(pbr, "metallicFactor");
    scalar(pbr, "roughnessFactor");
    std::vector<GltfTextureBinding> result;
    auto texture = [&](const Json& owner, const char* name, TextureSemantic semantic) {
        if (owner.contains(name))
            result.push_back(binding(s, owner.at(name), name, semantic));
    };
    constexpr auto color = TextureSemantic::Color, data = TextureSemantic::Data,
                   normal = TextureSemantic::Normal;
    texture(pbr, "baseColorTexture", color);
    texture(pbr, "metallicRoughnessTexture", data);
    texture(m, "normalTexture", normal);
    texture(m, "occlusionTexture", data);
    texture(m, "emissiveTexture", color);
    const bool unlit = declared_extension(s, m, "KHR_materials_unlit") != nullptr;
    const bool legacy = declared_extension(s, m, "KHR_materials_pbrSpecularGlossiness") != nullptr;
    if (const auto* e = declared_extension(s, m, "KHR_materials_pbrSpecularGlossiness")) {
        vector(*e, "diffuseFactor", 4);
        vector(*e, "specularFactor", 3);
        scalar(*e, "glossinessFactor");
        texture(*e, "diffuseTexture", color);
        texture(*e, "specularGlossinessTexture", color);
    }
    for (const auto name :
         {"KHR_materials_clearcoat", "KHR_materials_specular", "KHR_materials_sheen",
          "KHR_materials_anisotropy", "KHR_materials_iridescence", "KHR_materials_transmission",
          "KHR_materials_volume", "KHR_materials_ior", "KHR_materials_dispersion",
          "KHR_materials_emissive_strength"}) {
        const auto* e = declared_extension(s, m, name);
        if (!e)
            continue;
        const std::string_view kind = name;
        require(!unlit && (!legacy || kind == "KHR_materials_emissive_strength"),
                std::string(name) + " is incompatible with this material workflow");
        if (kind == "KHR_materials_clearcoat") {
            scalar(*e, "clearcoatFactor");
            scalar(*e, "clearcoatRoughnessFactor");
            texture(*e, "clearcoatTexture", data);
            texture(*e, "clearcoatRoughnessTexture", data);
            texture(*e, "clearcoatNormalTexture", normal);
        } else if (kind == "KHR_materials_specular") {
            scalar(*e, "specularFactor");
            vector(*e, "specularColorFactor", 3, 0, max_float);
            texture(*e, "specularTexture", data);
            texture(*e, "specularColorTexture", color);
        } else if (kind == "KHR_materials_sheen") {
            vector(*e, "sheenColorFactor", 3);
            scalar(*e, "sheenRoughnessFactor");
            texture(*e, "sheenColorTexture", color);
            texture(*e, "sheenRoughnessTexture", data);
        } else if (kind == "KHR_materials_anisotropy") {
            scalar(*e, "anisotropyStrength");
            scalar(*e, "anisotropyRotation", -max_float, max_float);
            texture(*e, "anisotropyTexture", data);
        } else if (kind == "KHR_materials_iridescence") {
            scalar(*e, "iridescenceFactor");
            scalar(*e, "iridescenceIor", 1, max_float);
            scalar(*e, "iridescenceThicknessMinimum", 0, max_float);
            scalar(*e, "iridescenceThicknessMaximum", 0, max_float);
            texture(*e, "iridescenceTexture", data);
            texture(*e, "iridescenceThicknessTexture", data);
        } else if (kind == "KHR_materials_transmission") {
            scalar(*e, "transmissionFactor");
            texture(*e, "transmissionTexture", data);
        } else if (kind == "KHR_materials_volume") {
            scalar(*e, "thicknessFactor", 0, max_float);
            vector(*e, "attenuationColor", 3);
            if (e->contains("attenuationDistance"))
                require(number(e->at("attenuationDistance"), 0, max_float) > 0,
                        "attenuation distance must be positive");
            texture(*e, "thicknessTexture", data);
        } else if (kind == "KHR_materials_ior") {
            const float ior = number(e->value("ior", Json(1.5)), 0, max_float);
            require(ior == 0 || ior >= 1, "IOR must be zero or at least one");
        } else if (kind == "KHR_materials_dispersion")
            scalar(*e, "dispersion", 0, max_float);
        else
            scalar(*e, "emissiveStrength", 0, max_float);
    }
    return result;
}
} // namespace
std::vector<GltfMaterialVariant> gltf_material_variants(const GltfSourceBundle& source) {
    std::vector<GltfMaterialVariant> result;
    if (const auto* root = declared_extension(source, source.document, "KHR_materials_variants")) {
        const auto& variants = array(*root, "variants", 65536);
        require(!variants.empty(), "material variants must not be empty");
        for (const auto& variant : variants) {
            object(variant);
            require(variant.contains("name") && variant.at("name").is_string(),
                    "material variant requires name");
            const auto name = variant.at("name").get<std::string>();
            require(name.size() <= 4096, "material variant name exceeds limit");
            result.push_back({name, {}});
        }
    }
    const auto& meshes = array(source.document, "meshes", 1000000);
    const auto materials = array(source.document, "materials", 1000000).size();
    std::size_t total = 0;
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        const auto& primitives = array(meshes[m], "primitives", 100000);
        for (std::size_t p = 0; p < primitives.size(); ++p) {
            const auto* ext = declared_extension(source, primitives[p], "KHR_materials_variants");
            if (!ext)
                continue;
            const auto& mappings = array(*ext, "mappings", 65536);
            require(!mappings.empty(), "variant mapping must not be empty");
            std::set<std::size_t> assigned;
            for (const auto& mapping : mappings) {
                object(mapping);
                const auto material = size_value(mapping.at("material"));
                require(material < materials, "variant material index is invalid");
                const auto& variants = array(mapping, "variants", 65536);
                require(!variants.empty() && variants.size() <= 1000000 - total,
                        "variant mapping exceeds limit or is empty");
                total += variants.size();
                for (const auto& entry : variants) {
                    const auto v = size_value(entry);
                    require(v < result.size(), "variant index is invalid");
                    require(assigned.insert(v).second,
                            "variant has conflicting primitive mappings");
                    result[v].mappings.emplace(std::pair{m, p}, material);
                }
            }
        }
    }
    return result;
}
void validate_gltf_surfaces(const GltfSourceBundle& source) {
    const auto& samplers = array(source.document, "samplers", 1000000);
    for (std::size_t i = 0; i < samplers.size(); ++i) {
        try {
            (void)sampler(samplers[i]);
        } catch (const std::exception& e) {
            throw std::runtime_error("glTF sampler " + std::to_string(i) + ": " + e.what());
        }
    }
    const auto& materials = array(source.document, "materials", 1000000);
    for (std::size_t i = 0; i < materials.size(); ++i) {
        try {
            (void)material(source, materials[i]);
        } catch (const std::exception& e) {
            throw std::runtime_error("glTF material " + std::to_string(i) + ": " + e.what());
        }
    }
    (void)gltf_material_variants(source);
}
std::vector<GltfTextureBinding> gltf_texture_bindings(const GltfSourceBundle& source,
                                                      std::size_t index) {
    const auto& materials = array(source.document, "materials", 1000000);
    require(index < materials.size(), "material index is invalid");
    auto result = material(source, materials[index]);
    const auto& m = materials[index];
    const bool legacy = extension(m, "KHR_materials_pbrSpecularGlossiness") != nullptr;
    const bool unlit = extension(m, "KHR_materials_unlit") != nullptr;
    std::erase_if(result, [&](const auto& b) {
        if (legacy)
            return b.role == "baseColorTexture" || b.role == "metallicRoughnessTexture";
        return unlit && b.role != "baseColorTexture";
    });
    return result;
}
GltfMaterialFactors gltf_material_factors(const NativeGltfDocument& source, std::size_t index) {
    using Native = Diligent::GLTF::Material;
    const auto& materials = array(source.source().document, "materials", 1000000);
    require(index < materials.size(), "material index is invalid");
    const auto& original = materials[index];
    // Use native factor handling, independently of its three-bit UV selector.
    // FORGE retains source UV sets and binding samplers in a separate value path.
    Diligent::GLTF::MaterialLoadContext context;
    context.NumTextureAttributes = 0;
    auto native =
        Diligent::GLTF::LoadMaterial(source.model(), source.model().materials.at(index), context);
    GltfMaterialFactors result;
    result.workflow = native.Attribs.Workflow == Native::PBR_WORKFLOW_UNLIT ? "unlit"
                      : native.Attribs.Workflow == Native::PBR_WORKFLOW_SPEC_GLOSS
                          ? "specular-glossiness"
                          : "metallic-roughness";
    result.alpha_mode = original.value("alphaMode", std::string("OPAQUE"));
    result.double_sided = native.DoubleSided;
    auto& v = result.values;
    const auto scalar_value = [&](const char* name, float value) {
        require(std::isfinite(value), std::string(name) + " native factor is not finite");
        v[name] = value;
    };
    const auto vector3 = [&](const char* name, const auto& value) {
        std::array<float, 3> data{value[0], value[1], value[2]};
        for (float x : data)
            require(std::isfinite(x), std::string(name) + " native factor is not finite");
        v[name] = data;
    };
    v["baseColorFactor"] =
        std::array<float, 4>{native.Attribs.BaseColorFactor.x, native.Attribs.BaseColorFactor.y,
                             native.Attribs.BaseColorFactor.z, native.Attribs.BaseColorFactor.w};
    scalar_value("alphaCutoff", native.Attribs.AlphaCutoff);
    scalar_value("metallicFactor", native.Attribs.MetallicFactor);
    scalar_value(result.workflow == "specular-glossiness" ? "glossinessFactor" : "roughnessFactor",
                 native.Attribs.RoughnessFactor);
    if (result.workflow == "specular-glossiness")
        vector3("specularFactor", native.Attribs.SpecularFactor);
    // Native LoadMaterial multiplies emission and omits occlusion strength.
    // Keep independently animatable source factors, and restore authored alpha
    // independently of native transmission's convenient alpha-blend policy.
    v["emissiveFactor"] =
        original.value("emissiveFactor", Json::array({0, 0, 0})).get<std::array<float, 3>>();
    v["emissiveStrength"] = 1.f;
    v["ior"] = 1.5f;
    v["dispersion"] = 0.f;
    v["occlusionStrength"] = optional_object(original, "occlusionTexture").value("strength", 1.f);
    v["normalScale"] = optional_object(original, "normalTexture").value("scale", 1.f);
    for (const auto [ext, key] : {std::pair{"KHR_materials_emissive_strength", "emissiveStrength"},
                                  {"KHR_materials_ior", "ior"},
                                  {"KHR_materials_dispersion", "dispersion"}})
        if (const auto* e = extension(original, ext); e && e->contains(key))
            v[key] = number(e->at(key), 0, max_float);
    scalar_value("clearcoatFactor", native.Attribs.ClearcoatFactor);
    scalar_value("clearcoatRoughnessFactor", native.Attribs.ClearcoatRoughnessFactor);
    v["clearcoatNormalScale"] = 1.f;
    if (const auto* e = extension(original, "KHR_materials_clearcoat"))
        v["clearcoatNormalScale"] =
            optional_object(*e, "clearcoatNormalTexture").value("scale", 1.f);
    if (native.Specular) {
        scalar_value("specularFactor", native.Specular->Factor);
        vector3("specularColorFactor", native.Specular->ColorFactor);
    }
    if (native.Sheen) {
        scalar_value("sheenRoughnessFactor", native.Sheen->RoughnessFactor);
        vector3("sheenColorFactor", native.Sheen->ColorFactor);
    }
    if (native.Anisotropy) {
        scalar_value("anisotropyStrength", native.Anisotropy->Strength);
        scalar_value("anisotropyRotation", native.Anisotropy->Rotation);
    }
    if (native.Iridescence) {
        scalar_value("iridescenceFactor", native.Iridescence->Factor);
        scalar_value("iridescenceIor", native.Iridescence->IOR);
        scalar_value("iridescenceThicknessMinimum", native.Iridescence->ThicknessMinimum);
        scalar_value("iridescenceThicknessMaximum", native.Iridescence->ThicknessMaximum);
    }
    if (native.Transmission)
        scalar_value("transmissionFactor", native.Transmission->Factor);
    if (native.Volume) {
        scalar_value("thicknessFactor", native.Volume->ThicknessFactor);
        // Missing distance means infinite attenuation length, not a fabricated
        // finite authored value. The renderer can select its no-absorption path.
        if (extension(original, "KHR_materials_volume")->contains("attenuationDistance"))
            scalar_value("attenuationDistance", native.Volume->AttenuationDistance);
        vector3("attenuationColor", native.Volume->AttenuationColor);
    }
    return result;
}
MaterialData cook_gltf_material(const NativeGltfDocument& source, std::size_t index) {
    const auto factors = gltf_material_factors(source, index);
    MaterialData result;
    result.model = "forge.gltf." + factors.workflow + ".v1";
    result.alpha = factors.alpha_mode == "MASK"    ? MaterialAlpha::Mask
                   : factors.alpha_mode == "BLEND" ? MaterialAlpha::Blend
                                                   : MaterialAlpha::Opaque;
    result.alpha_cutoff = std::get<float>(factors.values.at("alphaCutoff"));
    result.double_sided = factors.double_sided;
    result.depth_write = result.alpha != MaterialAlpha::Blend;
    for (const auto& [name, value] : factors.values) {
        if (name == "alphaCutoff")
            continue;
        MaterialParameter parameter;
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>)
                    parameter.value[0] = v;
                else {
                    std::copy(v.begin(), v.end(), parameter.value.begin());
                    // The vector-valued factors in this exact glTF material adapter
                    // are linear colors, including legacy specularFactor.
                    parameter.type = v.size() == 3 ? MaterialParameterType::LinearColor3
                                                   : MaterialParameterType::LinearColor4;
                }
            },
            value);
        result.parameters.emplace(name, parameter);
    }
    for (const auto& binding : gltf_texture_bindings(source.source(), index)) {
        MaterialTextureSlot slot;
        slot.semantic = binding.semantic;
        slot.sampler = binding.sampler;
        slot.uv_set = binding.uv_set;
        slot.offset = binding.offset;
        slot.scale = binding.scale;
        slot.rotation = binding.rotation;
        result.textures.emplace(binding.role, slot);
    }
    validate_material(result);
    return result;
}
} // namespace forge::asset_detail
