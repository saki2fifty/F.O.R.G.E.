#pragma once
#include <bit>
#include <forge/gltf_source.hpp>
inline forge::GltfSourceBundle gltf_instance_fixture(bool animated = false) {
    using Json = nlohmann::json;
    forge::GltfSourceBundle source;
    source.source = "instances.gltf";
    source.source_digest = std::string(64, 'a');
    auto& doc = source.document;
    doc = {{"asset", {{"version", "2.0"}}},
           {"accessors", Json::array()},
           {"bufferViews", Json::array()},
           {"extensionsUsed", {"EXT_mesh_gpu_instancing"}},
           {"extensionsRequired", {"EXT_mesh_gpu_instancing"}}};
    auto bytes = std::make_shared<std::vector<std::byte>>();
    auto attribute = [&](std::initializer_list<float> values, unsigned width) {
        const auto offset = bytes->size();
        for (float value : values) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned b = 0; b < 4; ++b)
                bytes->push_back(std::byte((bits >> (8 * b)) & 255));
        }
        doc["bufferViews"].push_back(
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", bytes->size() - offset}});
        const auto index = doc["accessors"].size();
        doc["accessors"].push_back({{"bufferView", index},
                                    {"componentType", 5126},
                                    {"count", values.size() / width},
                                    {"type", width == 3 ? "VEC3" : "VEC4"}});
        return index;
    };
    attribute({-1, -1, 0, 1, -1, 0, 0, 1, 0}, 3);
    doc["accessors"][0]["min"] = {-1, -1, 0};
    doc["accessors"][0]["max"] = {1, 1, 0};
    attribute({2, 0, 0, -2, 0, 0}, 3);
    attribute({0, 0, 0, 1, 0, 0, 0, 1}, 4);
    attribute({1, 1, 1, -1, 0, 2}, 3);
    doc["buffers"] = Json::array({{{"uri", "instances.bin"}, {"byteLength", bytes->size()}}});
    source.buffers.push_back({bytes, 0, bytes->size()});
    source.captured_bytes = bytes->size();
    doc["meshes"] =
        Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}}}})}}});
    doc["nodes"] = Json::array(
        {{{"name", "Pair"},
          {"mesh", 0},
          {"translation", {10, 0, 0}},
          {"extensions",
           {{"EXT_mesh_gpu_instancing",
             {{"attributes", {{"TRANSLATION", 1}, {"ROTATION", 2}, {"SCALE", 3}}}}}}}}});
    if (animated) {
        const auto offset = bytes->size();
        for (const float value : {0.f, 1.f}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned b = 0; b < 4; ++b)
                bytes->push_back(std::byte((bits >> (8 * b)) & 255));
        }
        doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", 8}});
        doc["accessors"].push_back({{"bufferView", 4},
                                    {"componentType", 5126},
                                    {"count", 2},
                                    {"type", "SCALAR"},
                                    {"min", {0}},
                                    {"max", {1}}});
        doc["meshes"][0]["primitives"][0]["targets"] = Json::array({{{"POSITION", 0}}});
        doc["nodes"][0]["weights"] = {.25};
        doc["animations"] = Json::array(
            {{{"samplers",
               Json::array({{{"input", 4}, {"output", 4}}, {{"input", 4}, {"output", 1}}})},
              {"channels",
               Json::array(
                   {{{"sampler", 0}, {"target", {{"node", 0}, {"path", "weights"}}}},
                    {{"sampler", 1}, {"target", {{"node", 0}, {"path", "translation"}}}}})}}});
        doc["buffers"][0]["byteLength"] = bytes->size();
        source.buffers[0].length = bytes->size();
        source.captured_bytes = bytes->size();
    }
    doc["scenes"] = Json::array({{{"nodes", {0}}}});
    doc["scene"] = 0;
    return source;
}
