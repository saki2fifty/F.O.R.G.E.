#include "gltf_model_cook.hpp"
#include "gltf_scene.hpp"
#include "model_importer.hpp"
#include "model_scene_values.hpp"
#include <iostream>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F fn, std::source_location where = std::source_location::current()) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid model scene accepted at " + std::to_string(where.line()));
}
void change(std::vector<ArtifactFile>& files, const std::function<void(Json&)>& edit) {
    for (auto& file : files)
        if (file.name == "model.json") {
            auto j = Json::parse(file.bytes);
            edit(j);
            const auto text = j.dump();
            const auto bytes = std::as_bytes(std::span(text));
            file.bytes.assign(bytes.begin(), bytes.end());
            return;
        }
    throw std::runtime_error("Missing model index fixture");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need official sample root");
        auto source = capture_gltf_source(argv[1], "NegativeScaleTest.gltf");
        auto& doc = source.document;
        doc["extensionsUsed"] = {"KHR_lights_punctual", "KHR_materials_variants",
                                 "KHR_node_visibility", "KHR_node_selectability"};
        doc["extensionsRequired"] = doc["extensionsUsed"];
        for (const auto& ext : doc["extensionsRequired"])
            require(model_cook_extensions().contains(ext.get<std::string>()),
                    "Recipe lacks admitted scene extension");
        doc["cameras"] = Json::array(
            {{{"type", "perspective"}, {"perspective", {{"yfov", 1.}, {"znear", .1}}}},
             {{"type", "orthographic"},
              {"orthographic", {{"xmag", -2.}, {"ymag", 3.}, {"znear", 0.}, {"zfar", 100.}}}}});
        doc["extensions"]["KHR_lights_punctual"] = {
            {"lights",
             Json::array({{{"type", "directional"}, {"intensity", 0}},
                          {{"type", "point"}, {"color", {.2, .5, 1.}}, {"spot", Json::object()}},
                          {{"type", "spot"},
                           {"range", 25.},
                           {"spot", {{"innerConeAngle", .1}, {"outerConeAngle", .5}}}}})}};
        doc["nodes"][0]["camera"] = 0;
        doc["nodes"][1]["camera"] = 1;
        doc["nodes"][0]["extensions"]["KHR_lights_punctual"] = {{"light", 2}};
        doc["nodes"][0]["extensions"]["KHR_node_visibility"] = {{"visible", false}};
        doc["nodes"][0]["extensions"]["KHR_node_selectability"] = {{"selectable", true}};
        doc["nodes"][1]["extensions"]["KHR_lights_punctual"] = {{"light", 1}};
        doc["nodes"][1]["extensions"]["KHR_node_selectability"] = {{"selectable", false}};
        const auto material = doc["materials"].size();
        doc["materials"].push_back(
            {{"name", "Alternate normal UV"}, {"normalTexture", {{"index", 0}, {"texCoord", 1}}}});
        auto& primitive = doc["meshes"][0]["primitives"][0];
        primitive["attributes"]["TEXCOORD_1"] = primitive["attributes"]["TEXCOORD_0"];
        doc["extensions"]["KHR_materials_variants"] = {
            {"variants", Json::array({{{"name", "Same label"}}, {{"name", "Same label"}}})}};
        primitive["extensions"]["KHR_materials_variants"] = {
            {"mappings", Json::array({{{"material", material}, {"variants", {0, 1}}}})}};
        const auto original = doc;
        const auto files = cook_static_gltf_bundle(NativeGltfDocument(source));
        const auto bundle = validate_model_bundle(files);
        const auto& h = bundle.hierarchy;
        require(doc == original, "Cooking changed model source");
        require(h["cameras"].size() == 2 && h["cameras"][0]["perspective"]["zfar"].is_null() &&
                    h["cameras"][0]["perspective"]["aspectRatio"].is_null() &&
                    h["cameras"][1]["orthographic"]["xmag"] == -2.,
                "Camera import lost infinite/autoaspect/signed magnification semantics");
        require(h["lights"].size() == 3 && h["lights"][1]["range"].is_null() &&
                    !h["lights"][1].contains("spot") && h["lights"][2]["range"] == 25. &&
                    h["lights"][0]["intensity"] == 0,
                "Light default/units/range values changed");
        require(h["nodes"][0]["visible"] == false && h["nodes"][0]["selectable"] == true &&
                    h["nodes"][0]["camera"] == 0 && h["nodes"][0]["light"] == 2 &&
                    h["nodes"][1]["selectable"] == false,
                "Independent node camera/light/interaction flags changed");
        require(h["material_variants"].size() == 2 &&
                    h["material_variants"][0]["name"] == h["material_variants"][1]["name"] &&
                    h["material_variants"][1]["mappings"][0]["material"] ==
                        "/materials/" + std::to_string(material),
                "Variant labels/mappings lost");
        bool bound = false;
        for (const auto& member : bundle.members)
            if (member.identity.address == "/meshes/0")
                bound = member.bindings.at("material." + std::to_string(material + 1)) ==
                        "/materials/" + std::to_string(material);
        require(bound, "Variant material missing from mesh dependency graph");
        auto bad = [&](const std::function<void(Json&)>& edit) {
            auto copy = source;
            edit(copy.document);
            rejects([&] { cook_static_gltf_bundle(NativeGltfDocument(copy)); });
        };
        bad([](auto& j) { j["nodes"][0]["extensions"]["KHR_lights_punctual"]["light"] = 99; });
        bad([](auto& j) { j["extensions"]["KHR_lights_punctual"]["lights"][0]["range"] = 1.; });
        bad([](auto& j) { j["extensions"]["KHR_lights_punctual"]["lights"][1]["color"][0] = 2.; });
        bad([](auto& j) {
            j["extensions"]["KHR_lights_punctual"]["lights"][1]["intensity"] = -1.;
        });
        bad([](auto& j) { j["extensions"]["KHR_lights_punctual"]["lights"][1]["range"] = 0.; });
        bad([](auto& j) {
            j["extensions"]["KHR_lights_punctual"]["lights"][2]["spot"]["innerConeAngle"] = .6;
        });
        bad([](auto& j) { j["extensions"]["KHR_lights_punctual"]["lights"][2].erase("spot"); });
        bad([](auto& j) { j["cameras"][0]["perspective"]["zfar"] = nullptr; });
        bad([](auto& j) { j["cameras"][0]["perspective"]["aspectRatio"] = 0.; });
        bad([](auto& j) { j["nodes"][0]["extensions"]["KHR_node_visibility"]["visible"] = 0; });
        bad([](auto& j) {
            j["nodes"][1]["extensions"]["KHR_node_selectability"]["selectable"] = "false";
        });
        bad([](auto& j) { j["extensionsUsed"].erase(j["extensionsUsed"].begin()); });
        bad([](auto& j) { j["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_1"); });
        auto invalid = [&](const std::function<void(Json&)>& edit) {
            auto copy = files;
            change(copy, edit);
            rejects([&] { validate_model_bundle(copy); });
        };
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["camera"] = 99; });
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["light"] = -1; });
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["selectable"] = 0; });
        invalid([](auto& j) { j["hierarchy"]["cameras"][0]["perspective"]["znear"] = 0; });
        invalid([](auto& j) { j["hierarchy"]["lights"][2]["spot"]["outerConeAngle"] = 0; });
        invalid([](auto& j) {
            j["hierarchy"]["material_variants"][0]["mappings"][0]["primitive"] = 999;
        });
        invalid([](auto& j) {
            auto v = j["hierarchy"]["material_variants"][0]["mappings"][0];
            j["hierarchy"]["material_variants"][0]["mappings"].push_back(v);
        });
        invalid([](auto& j) {
            j["hierarchy"]["material_variants"][0]["mappings"][0]["mesh"] = "/images/0";
        });
        invalid([&](auto& j) {
            for (auto& m : j["members"])
                if (m["address"] == "/meshes/0")
                    m["bindings"].erase("material." + std::to_string(material + 1));
        });
        std::cout << "Model cameras, punctual lights, interaction metadata, variants, alternate "
                     "UVs and candidate rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
