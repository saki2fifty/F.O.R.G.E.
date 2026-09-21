#include "gltf_instance_fixture.hpp"
#include "gltf_native.hpp"
#include <bit>
#include <cmath>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <tiny_gltf.h>

using namespace forge;
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } cleanup{root};
        auto require = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        std::vector<unsigned char> geometry;
        for (const float value : {-1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                geometry.push_back(static_cast<unsigned char>((bits >> (8 * i)) & 255));
        }
        {
            std::ofstream out(root / "geometry.bin", std::ios::binary);
            out.write(reinterpret_cast<const char*>(geometry.data()),
                      static_cast<std::streamsize>(geometry.size()));
            if (!out)
                throw std::runtime_error("Cannot write fixture buffer");
        }
        atomic_write(root / "model.GLTF", R"({"asset":{"version":"2.0"},
            "buffers":[{"byteLength":36,"uri":"geometry.bin"}],
            "bufferViews":[{"buffer":0,"byteLength":36,"target":34962}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,0],"max":[1,1,0]}],
            "images":[{"uri":"data:image/png;base64,AAECAw=="}],
            "textures":[{"source":0}],"materials":[{"name":"Fixture","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
            "scenes":[{"nodes":[0]}],"nodes":[{"name":"root","mesh":0}],"scene":0})");
        auto source = capture_gltf_source(root, "model.GLTF");
        std::filesystem::remove(root / "model.GLTF");
        std::filesystem::remove(root / "geometry.bin");
        asset_detail::NativeGltfDocument document(std::move(source));
        const auto& model = document.model();
        require(model.meshes.size() == 1 && model.nodes.size() == 1 && model.materials.size() == 1,
                "Native model metadata changed");
        require(model.buffers.at(0).data == geometry && model.accessors.at(0).count == 3,
                "Native model did not use captured geometry");
        const auto positions = document.floats(0);
        require(positions.count == 3 && positions.components == 3 &&
                    positions.values == std::vector<float>{-1, 0, 0, 1, 0, 0, 0, 1, 0},
                "Native attribute conversion changed positions");
        require(model.images.size() == 1 && document.source().images[0].encoded.length == 4,
                "Captured image metadata/encoded bytes lost");
        require(model.images[0].image.empty(),
                "Native adapter unexpectedly decoded/copied image pixels");
        require(document.captured_reads() == 3,
                "Native adapter did not read its captured manifest exclusively");
        require(document.source().document.at("buffers")[0].at("uri") == "geometry.bin",
                "Virtual transport altered source provenance");
        auto invalid = document.source();
        invalid.document["accessors"][0]["count"] = UINT64_MAX;
        bool rejected = false;
        try {
            asset_detail::NativeGltfDocument bad(std::move(invalid));
        } catch (const std::exception& error) {
            rejected = std::string_view(error.what()).find("accessor") != std::string_view::npos;
        }
        require(rejected, "Invalid accessor reached native parser");
        using Json = nlohmann::json;
        auto decode = [&](Json accessor, Json views, std::vector<unsigned char> bytes) {
            Json source_doc{
                {"asset", {{"version", "2.0"}}},
                {"buffers", Json::array({{{"byteLength", bytes.size()}, {"uri", "decode.bin"}}})},
                {"bufferViews", std::move(views)},
                {"accessors", Json::array({std::move(accessor)})}};
            {
                std::ofstream out(root / "decode.bin", std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
                if (!out)
                    throw std::runtime_error("Cannot write decode fixture");
            }
            atomic_write(root / "decode.gltf", source_doc.dump());
            return asset_detail::NativeGltfDocument(capture_gltf_source(root, "decode.gltf"));
        };
        const auto single_view = Json::array({{{"buffer", 0}, {"byteLength", 4}}});
        auto normalized = decode({{"bufferView", 0},
                                  {"componentType", 5121},
                                  {"type", "VEC4"},
                                  {"count", 1},
                                  {"normalized", true}},
                                 single_view, {0, 255, 0, 128});
        const auto values = normalized.floats(0);
        require(values.values[0] == 0 && values.values[1] == 1 &&
                    std::abs(values.values[3] - 128.f / 255.f) < 0.000001f,
                "Native normalized integer conversion differs from glTF");
        bool normalized_ids_rejected = false;
        try {
            (void)normalized.unsigned_integers(0);
        } catch (const std::exception&) {
            normalized_ids_rejected = true;
        }
        require(normalized_ids_rejected, "Normalized attributes silently became integer IDs");
        auto integers =
            decode({{"bufferView", 0}, {"componentType", 5125}, {"type", "SCALAR"}, {"count", 1}},
                   single_view, {254, 255, 255, 255});
        require(integers.unsigned_integers(0).values[0] == 4294967294u,
                "Native index conversion lost uint32 precision");
        auto matrix =
            decode({{"bufferView", 0}, {"componentType", 5121}, {"type", "MAT2"}, {"count", 1}},
                   Json::array({{{"buffer", 0}, {"byteLength", 8}}}), {1, 2, 99, 99, 3, 4, 99, 99});
        require(matrix.floats(0).values == std::vector<float>{1, 2, 3, 4},
                "Native matrix conversion consumed padding as components");
        auto sparse = decode({{"componentType", 5126},
                              {"type", "SCALAR"},
                              {"count", 3},
                              {"sparse",
                               {{"count", 1},
                                {"indices", {{"bufferView", 0}, {"componentType", 5121}}},
                                {"values", {{"bufferView", 1}}}}}},
                             Json::array({{{"buffer", 0}, {"byteLength", 1}},
                                          {{"buffer", 0}, {"byteOffset", 4}, {"byteLength", 4}}}),
                             {1, 0, 0, 0, 0, 0, 128, 63});
        require(sparse.floats(0).values == std::vector<float>{0, 1, 0},
                "Native sparse patches or zero-backed values are incorrect");
        {
            auto input = gltf_instance_fixture();
            const auto captured = input.document;
            asset_detail::NativeGltfDocument copies(input);
            require(copies.source().document == captured && copies.model().nodes.size() == 1 &&
                        copies.scene_source().document.at("nodes").size() == 3,
                    "Instance expansion changed captured source/native provenance");
            const auto& hierarchy = copies.hierarchy();
            require(hierarchy.nodes[0].mesh == asset_detail::gltf_no_index &&
                        hierarchy.nodes[1].parent == 0 && hierarchy.nodes[2].parent == 0 &&
                        hierarchy.nodes[1].matrix[12] == 2 && hierarchy.nodes[2].matrix[12] == -2 &&
                        hierarchy.nodes[2].matrix[0] == -1 && hierarchy.nodes[2].matrix[5] == 0,
                    "Instancing lost transform order, signed/zero scale or drew an extra original "
                    "mesh");
            {
                auto quantized = input;
                auto bytes = std::make_shared<std::vector<std::byte>>(
                    input.buffers[0].bytes().begin(), input.buffers[0].bytes().end());
                const auto offset = bytes->size();
                for (auto value : {0, 0, 90, 90, 0, 0, 0, 127})
                    bytes->push_back(std::byte(value));
                quantized.document["bufferViews"].push_back(
                    {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", 8}});
                quantized.document["accessors"][2] = {{"bufferView", 4},
                                                      {"componentType", 5120},
                                                      {"normalized", true},
                                                      {"count", 2},
                                                      {"type", "VEC4"}};
                quantized.document["buffers"][0]["byteLength"] = bytes->size();
                quantized.buffers[0] = {bytes, 0, bytes->size()};
                asset_detail::NativeGltfDocument integer_rotation(quantized);
                const auto& matrix = integer_rotation.hierarchy().nodes[1].matrix;
                require(
                    std::abs(matrix[0]) < 1e-6 && std::abs(matrix[1] - 1) < 1e-6,
                    "Normalized byte quaternion quantization was rejected or changed orientation");
            }
            {
                asset_detail::NativeGltfDocument animated(gltf_instance_fixture(true));
                const auto clip = animated.animation(0);
                require(clip.tracks.size() == 3 &&
                            animated.hierarchy().nodes[0].morph_weights.empty() &&
                            animated.hierarchy().nodes[1].morph_weights ==
                                std::vector<double>{.25} &&
                            animated.hierarchy().nodes[2].morph_weights == std::vector<double>{.25},
                        "Instance morph defaults or weight-channel expansion changed");
                std::set<std::size_t> morph_nodes;
                for (const auto& track : clip.tracks)
                    if (track.path == asset_detail::NativeAnimationPath::Weights)
                        morph_nodes.insert(track.node);
                    else
                        require(track.node == 0, "Shared TRS animation left the parent");
                require(morph_nodes == std::set<std::size_t>{1, 2},
                        "Morph animation did not reach both instances");
            }
            auto rejects_instance = [&](auto edit) {
                auto invalid = input;
                edit(invalid.document);
                bool failed = false;
                try {
                    asset_detail::NativeGltfDocument bad(std::move(invalid));
                } catch (const std::exception&) {
                    failed = true;
                }
                require(failed, "Malformed instance attributes reached model realization");
            };
            rejects_instance([](auto& d) { d["accessors"][3]["count"] = 1; });
            rejects_instance([](auto& d) { d["accessors"][2]["type"] = "VEC3"; });
            rejects_instance([](auto& d) { d["nodes"][0].erase("mesh"); });
            rejects_instance([](auto& d) { d["extensionsUsed"] = nlohmann::json::array(); });
            rejects_instance([](auto& d) {
                d["nodes"][0]["extensions"]["EXT_mesh_gpu_instancing"]["attributes"]["SCALE"] =
                    9000;
            });
            input.document["nodes"][0]["extensions"]["EXT_mesh_gpu_instancing"]["attributes"]
                          ["_CUSTOM"] = 1;
            asset_detail::NativeGltfDocument custom(input);
            require(custom.scene_source().diagnostics.size() == 1 &&
                        custom.source().document == input.document,
                    "Custom instance payload was silently consumed or discarded");
        }
        std::cout << "Native Diligent model metadata uses captured geometry and deferred encoded "
                     "images without disk/device\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
