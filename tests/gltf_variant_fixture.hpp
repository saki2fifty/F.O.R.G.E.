#pragma once
#include "gltf_lod_fixture.hpp"
inline forge::GltfSourceBundle gltf_variant_fixture() {
    auto source = gltf_lod_fixture();
    auto& d = source.document;
    d["extensionsUsed"].push_back("KHR_materials_variants");
    d["extensionsRequired"].push_back("KHR_materials_variants");
    d["extensions"]["KHR_materials_variants"]["variants"] =
        nlohmann::json::array({{{"name", "Split surfaces"}}, {{"name", "Partial surface"}}});
    d["materials"].push_back({{"name", "Alternate surface"},
                              {"pbrMetallicRoughness", {{"baseColorFactor", {.1, .7, .2, 1}}}}});
    auto mapping = [&](unsigned mesh, unsigned part, unsigned material,
                       std::vector<unsigned> variants) {
        d["meshes"][mesh]["primitives"][part]["extensions"]["KHR_materials_variants"]["mappings"] =
            nlohmann::json::array({{{"material", material}, {"variants", std::move(variants)}}});
    };
    mapping(0, 0, 1, {0, 1});
    mapping(0, 1, 2, {0});
    mapping(1, 0, 2, {0});
    return source;
}
