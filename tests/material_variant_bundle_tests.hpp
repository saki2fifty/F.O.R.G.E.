#pragma once
#include "gltf_variant_fixture.hpp"
inline void check_material_variant_bundle() {
    auto source = gltf_variant_fixture();
    auto files = cook_static_gltf_bundle(NativeGltfDocument(source));
    const auto index = validate_model_bundle(files);
    require(index.version == 4 && index.hierarchy.at("material_variants").size() == 2,
            "Material variant bundle version/metadata missing");
    unsigned count = 0;
    for (const auto& member : index.members)
        if (member.material_variant) {
            ++count;
            require(member.artifact.file.empty() && member.identity.type == "material_variant",
                    "Inline variant duplicated its authenticated hierarchy");
        }
    require(count == 2, "Material variants lack persistent logical members");
    auto corrupt = files;
    change(corrupt, [](Json& j) {
        j["hierarchy"]["material_variants"][0]["mappings"][0]["primitive"] = 99;
    });
    rejects([&] { validate_model_bundle(corrupt); });
    corrupt = files;
    change(corrupt,
           [](Json& j) { j["hierarchy"]["material_variants"][0]["mappings"][0]["lod"] = 15; });
    rejects([&] { validate_model_bundle(corrupt); });
    corrupt = files;
    change(corrupt, [](Json& j) {
        for (auto& m : j["members"])
            if (m.contains("material_variant")) {
                m["material_variant"] = 99;
                break;
            }
    });
    rejects([&] { validate_model_bundle(corrupt); });
    corrupt = files;
    change(corrupt, [](Json& j) { j["version"] = 3; });
    rejects([&] { validate_model_bundle(corrupt); });
    corrupt = files;
    change(corrupt, [](Json& j) {
        for (auto& m : j["members"])
            if (m.contains("material_variant")) {
                m["bindings"]["material:/materials/1"] = "/materials/0";
                break;
            }
    });
    rejects([&] { validate_model_bundle(corrupt); });
    // Legacy version3 carries metadata without a persistent selector. It remains
    // readable, but cannot acquire a guessed variant identity from array position.
    change(files, [](Json& j) {
        j["version"] = 3;
        auto& members = j["members"];
        for (auto it = members.begin(); it != members.end();) {
            if (it->contains("material_variant"))
                it = members.erase(it);
            else
                ++it;
        }
        for (auto& v : j["hierarchy"]["material_variants"]) {
            auto& mappings = v["mappings"];
            for (auto it = mappings.begin(); it != mappings.end();) {
                if (it->value("lod", 0) != 0)
                    it = mappings.erase(it);
                else
                    ++it;
            }
        }
    });
    require(validate_model_bundle(files).version == 3,
            "Legacy model variants were made unreadable");
}
