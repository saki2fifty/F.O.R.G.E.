#include "gltf_native.hpp"
#include "gltf_surfaces.hpp"
#include <cmath>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn, std::string_view why) {
    try {
        fn();
    } catch (const std::exception& e) {
        if (std::string_view(e.what()).find(why) != std::string_view::npos)
            return;
        throw std::runtime_error("Unexpected surface rejection: " + std::string(e.what()));
    }
    throw std::runtime_error("Invalid surface admitted");
}
GltfSourceBundle fixture() {
    GltfSourceBundle s;
    s.document = {
        {"asset", {{"version", "2.0"}}},
        {"images", Json::array({{{"uri", "a.png"}}})},
        {"textures", Json::array({{{"source", 0}, {"sampler", 0}}})},
        {"samplers", Json::array({Json::object()})},
        {"materials", Json::array({{{"pbrMetallicRoughness",
                                     {{"baseColorFactor", {0.2, 0.3, 0.4, 0.5}},
                                      {"baseColorTexture", {{"index", 0}}},
                                      {"metallicRoughnessTexture", {{"index", 0}}}}},
                                    {"normalTexture", {{"index", 0}, {"scale", -2.0}}},
                                    {"occlusionTexture", {{"index", 0}, {"strength", 0.2}}}}})}};
    auto bytes = std::make_shared<std::vector<std::byte>>(4);
    s.images = {{{bytes, 0, 4}, "image/png"}};
    return s;
}
void extension(GltfSourceBundle& s, const char* name, Json data) {
    if (!s.document.contains("extensionsUsed"))
        s.document["extensionsUsed"] = Json::array();
    s.document["extensionsUsed"].push_back(name);
    s.document["materials"][0]["extensions"][name] = std::move(data);
}
float scalar(const GltfMaterialFactors& m, const char* name) {
    return std::get<float>(m.values.at(name));
}
} // namespace
int main() {
    try {
        auto source = fixture();
        const auto original = source.document;
        NativeGltfDocument native(source);
        auto factors = gltf_material_factors(native, 0);
        require(factors.workflow == "metallic-roughness" && factors.alpha_mode == "OPAQUE",
                "Default material workflow changed");
        require(scalar(factors, "normalScale") == -2.f &&
                    scalar(factors, "occlusionStrength") == 0.2f,
                "Independent texture scale/strength lost");
        const auto bindings = gltf_texture_bindings(source, 0);
        require(bindings.size() == 4 && bindings[0].image == bindings[1].image &&
                    bindings[0].semantic == TextureSemantic::Color &&
                    bindings[1].semantic == TextureSemantic::Data &&
                    bindings[2].semantic == TextureSemantic::Normal,
                "One image lost distinct material usages");
        require(source.document == original, "Surface processing changed source");
        for (const auto filter : {9728, 9729, 9984, 9985, 9986, 9987}) {
            auto s = source;
            s.document["samplers"][0] = {
                {"minFilter", filter}, {"magFilter", 9728}, {"wrapS", 33648}, {"wrapT", 33071}};
            const auto b = gltf_texture_bindings(s, 0).at(0);
            require(b.sampler.u == TextureWrap::MirroredRepeat &&
                        b.sampler.v == TextureWrap::ClampEdge &&
                        b.sampler.mag == TextureFilter::Nearest,
                    "Sampler binding changed");
            require((b.sampler.max_lod == 0) == (filter == 9728 || filter == 9729),
                    "No-mip filter lost LOD clamp");
            require((b.sampler.min == TextureFilter::Nearest) ==
                        (filter == 9728 || filter == 9984 || filter == 9986),
                    "Minification filter changed");
            require((b.sampler.mip == TextureFilter::Nearest) == (filter == 9984 || filter == 9985),
                    "Mip interpolation changed");
        }
        auto changed = source;
        changed.document["extensionsUsed"] = {"KHR_texture_transform"};
        auto& info = changed.document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"];
        info["texCoord"] = 1;
        info["extensions"]["KHR_texture_transform"] = {
            {"texCoord", 27}, {"offset", {0.25, 0.5}}, {"scale", {-1, 0}}, {"rotation", 0.8}};
        const auto transformed = gltf_texture_bindings(changed, 0).at(0);
        require(transformed.uv_set == 27 && transformed.offset[0] == 0.25f &&
                    transformed.scale[0] == -1 && transformed.scale[1] == 0,
                "UV transform/index clamped to native selector bits");
        (void)gltf_material_factors(NativeGltfDocument(changed), 0);
        changed = source;
        extension(changed, "KHR_materials_ior", {{"ior", 0}});
        extension(changed, "KHR_materials_transmission", {{"transmissionFactor", 0.8}});
        extension(changed, "KHR_materials_dispersion", {{"dispersion", 2}});
        extension(changed, "KHR_materials_emissive_strength", {{"emissiveStrength", 4}});
        changed.document["materials"][0]["emissiveFactor"] = {0.5, 0.25, 0.125};
        factors = gltf_material_factors(NativeGltfDocument(changed), 0);
        require(factors.alpha_mode == "OPAQUE" && scalar(factors, "ior") == 0 &&
                    scalar(factors, "dispersion") == 2,
                "Native convenience policy rewrote authored alpha/IOR");
        require(std::get<std::array<float, 3>>(factors.values.at("emissiveFactor"))[0] == 0.5f &&
                    scalar(factors, "emissiveStrength") == 4,
                "Emission channels combined");
        changed = source;
        extension(changed, "KHR_materials_specular",
                  {{"specularFactor", 0.3}, {"specularColorFactor", {2, 3, 4}}});
        extension(changed, "KHR_materials_iridescence",
                  {{"iridescenceThicknessMinimum", 500}, {"iridescenceThicknessMaximum", 20}});
        extension(
            changed, "KHR_materials_clearcoat",
            {{"clearcoatFactor", 0.7}, {"clearcoatNormalTexture", {{"index", 0}, {"scale", -3}}}});
        extension(changed, "KHR_materials_sheen",
                  {{"sheenColorFactor", {0.2, 0.3, 0.4}}, {"sheenRoughnessFactor", 0.6}});
        extension(changed, "KHR_materials_anisotropy",
                  {{"anisotropyStrength", 0.8}, {"anisotropyRotation", -2}});
        extension(changed, "KHR_materials_volume",
                  {{"thicknessFactor", 2},
                   {"attenuationDistance", 3},
                   {"attenuationColor", {0.4, 0.5, 0.6}}});
        changed.document["materials"][0]["alphaCutoff"] = 2;
        factors = gltf_material_factors(NativeGltfDocument(changed), 0);
        require(scalar(factors, "alphaCutoff") == 2 &&
                    scalar(factors, "iridescenceThicknessMinimum") == 500 &&
                    scalar(factors, "iridescenceThicknessMaximum") == 20,
                "Valid unbounded/reversed material ranges rejected");
        require(std::get<std::array<float, 3>>(factors.values.at("specularColorFactor"))[2] == 4 &&
                    scalar(factors, "clearcoatNormalScale") == -3,
                "Extended color/normal scale narrowed");
        require(scalar(factors, "sheenRoughnessFactor") == 0.6f &&
                    scalar(factors, "anisotropyRotation") == -2 &&
                    scalar(factors, "attenuationDistance") == 3,
                "Native extension factors lost");
        changed = source;
        extension(changed, "KHR_materials_unlit", Json::object());
        require(gltf_texture_bindings(changed, 0).size() == 1 &&
                    gltf_material_factors(NativeGltfDocument(changed), 0).workflow == "unlit",
                "Unlit retained unused PBR bindings");
        changed = source;
        extension(changed, "KHR_materials_pbrSpecularGlossiness",
                  {{"diffuseTexture", {{"index", 0}}}});
        extension(changed, "KHR_materials_emissive_strength", {{"emissiveStrength", 2}});
        factors = gltf_material_factors(NativeGltfDocument(changed), 0);
        require(factors.workflow == "specular-glossiness" &&
                    std::get<std::array<float, 4>>(factors.values.at("baseColorFactor"))[0] == 1.f,
                "Legacy material inherited core fallback factor");
        const auto legacy_bindings = gltf_texture_bindings(changed, 0);
        require(std::none_of(legacy_bindings.begin(), legacy_bindings.end(),
                             [](const auto& b) {
                                 return b.role == "baseColorTexture" ||
                                        b.role == "metallicRoughnessTexture";
                             }),
                "Legacy workflow retained fallback texture bindings");
        for (const auto name : {"KHR_texture_basisu", "EXT_texture_webp"}) {
            changed = source;
            changed.document["extensionsUsed"] = {name};
            changed.document["extensionsRequired"] = {name};
            changed.document["textures"][0].erase("source");
            changed.document["textures"][0]["extensions"][name] = {{"source", 0}};
            changed.images[0].mime_type =
                std::string_view(name) == "KHR_texture_basisu" ? "image/ktx2" : "image/webp";
            require(gltf_texture_bindings(changed, 0).at(0).image_extension == name,
                    "Texture encoding selection lost");
            changed.document["extensionsRequired"] = Json::array();
            rejects([&] { validate_gltf_surfaces(changed); }, "without fallback");
        }
        changed = source;
        extension(changed, "KHR_materials_volume", Json::object());
        require(!gltf_material_factors(NativeGltfDocument(changed), 0)
                     .values.contains("attenuationDistance"),
                "Infinite default attenuation became a finite value");
        changed = source;
        changed.document["extensionsUsed"] = {"KHR_materials_variants"};
        changed.document["extensions"]["KHR_materials_variants"] = {
            {"variants", Json::array({{{"name", "Paint"}}, {{"name", "Paint"}}})}};
        changed.document["materials"].push_back(Json::object());
        Json variant_primitive = Json::object();
        variant_primitive["extensions"]["KHR_materials_variants"]["mappings"] =
            Json::array({{{"material", 1}, {"variants", {0}}}});
        changed.document["meshes"] =
            Json::array({{{"primitives", Json::array({variant_primitive})}}});
        auto variants = gltf_material_variants(changed);
        require(variants.size() == 2 && variants[0].name == variants[1].name &&
                    variants[0].mappings.at({0, 0}) == 1 && variants[1].mappings.empty(),
                "Variant addresses used names as identity or lost fallback");
        changed
            .document["meshes"][0]["primitives"][0]["extensions"]["KHR_materials_variants"]
                     ["mappings"]
            .push_back({{"material", 0}, {"variants", {0}}});
        rejects([&] { (void)gltf_material_variants(changed); }, "conflicting");
        const auto invalid = [&](auto edit, std::string_view message) {
            auto s = source;
            edit(s);
            rejects([&] { validate_gltf_surfaces(s); }, message);
        };
        invalid([](auto& s) { s.document["samplers"][0]["wrapS"] = 0; }, "wrap");
        invalid([](auto& s) { s.document["samplers"][0]["minFilter"] = 1; }, "minification");
        invalid([](auto& s) { s.document["materials"][0]["alphaMode"] = "UNKNOWN"; }, "alpha");
        invalid([](auto& s) { s.document["materials"][0]["doubleSided"] = 1; }, "boolean");
        invalid([](auto& s) { s.document["materials"][0]["normalTexture"]["index"] = 99; },
                "index");
        invalid([](auto& s) { s.document["textures"][0]["source"] = 99; }, "index");
        invalid(
            [](auto& s) {
                s.document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {1, 2, 3};
            },
            "shape");
        invalid([](auto& s) { extension(s, "KHR_materials_ior", {{"ior", 0.5}}); }, "IOR");
        invalid([](auto& s) { extension(s, "KHR_materials_volume", {{"attenuationDistance", 0}}); },
                "positive");
        invalid(
            [](auto& s) {
                extension(s, "KHR_materials_unlit", Json::object());
                extension(s, "KHR_materials_sheen", Json::object());
            },
            "incompatible");
        invalid(
            [](auto& s) {
                s.document["materials"][0]["extensions"]["KHR_materials_ior"] = {{"ior", 1.2}};
            },
            "undeclared");
        std::cout << "glTF surface factors, bindings, sampler and admission checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
