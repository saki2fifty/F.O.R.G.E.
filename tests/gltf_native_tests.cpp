#include "gltf_native.hpp"
#include <bit>
#include <cmath>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
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
        std::cout << "Native Diligent model metadata uses captured geometry and deferred encoded "
                     "images without disk/device\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
