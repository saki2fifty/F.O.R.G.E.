#pragma once
#include "gltf_instance_fixture.hpp"
inline forge::GltfSourceBundle gltf_lod_fixture() {
    using Json = nlohmann::json;
    auto source = gltf_instance_fixture();
    auto& doc = source.document;
    doc["extensionsUsed"] = {"MSFT_lod"};
    doc["extensionsRequired"] = {"MSFT_lod"};
    doc["meshes"].push_back(doc["meshes"][0]);
    doc["meshes"][0]["primitives"].push_back(doc["meshes"][0]["primitives"][0]);
    doc["materials"] = Json::array({{{"name", "High surface"}}, {{"name", "Low surface"}}});
    for (auto& part : doc["meshes"][0]["primitives"])
        part["material"] = 0;
    doc["meshes"][1]["primitives"][0]["material"] = 1;
    doc["nodes"] =
        Json::array({{{"name", "Detail"},
                      {"mesh", 0},
                      {"extensions", {{"MSFT_lod", {{"ids", {1}}}}}},
                      {"extras", {{"MSFT_screencoverage", {.4, .01}}}}},
                     {{"name", "Coarse"}, {"mesh", 1}},
                     {{"name", "Unrelated mesh reuse"}, {"mesh", 0}, {"translation", {3, 0, 0}}}});
    doc["scenes"][0]["nodes"] = {0, 2};
    return source;
}
