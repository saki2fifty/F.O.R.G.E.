#include "gltf_meshopt.hpp"
#include "gltf_native.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <forge/scene.hpp>
#include <iostream>
#include <meshoptimizer.h>

using namespace forge;
using namespace forge::asset_detail;
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::byte>;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view expected) {
    try {
        fn();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos)
            return;
        throw std::runtime_error("Unexpected rejection: " + std::string(error.what()));
    }
    throw std::runtime_error("Invalid meshopt source accepted");
}
Bytes encode(std::span<const std::byte> values, std::size_t stride, int version = 0) {
    const auto count = values.size() / stride;
    Bytes encoded(meshopt_encodeVertexBufferBound(count, stride));
    const auto size =
        meshopt_encodeVertexBufferLevel(reinterpret_cast<unsigned char*>(encoded.data()),
                                        encoded.size(), values.data(), count, stride, 2, version);
    require(size != 0, "Fixture vertex encoding failed");
    encoded.resize(size);
    return encoded;
}
struct Fixture {
    Json doc{{"asset", {{"version", "2.0"}}},
             {"extensionsUsed", {"EXT_meshopt_compression"}},
             {"extensionsRequired", {"EXT_meshopt_compression"}},
             {"buffers", Json::array()},
             {"bufferViews", Json::array()},
             {"accessors", Json::array()}};
    Bytes compressed;
    std::size_t decoded = 0;
    std::size_t add(Bytes input, std::size_t count, std::size_t stride,
                    std::string mode = "ATTRIBUTES", std::string filter = "NONE") {
        const auto index = doc["bufferViews"].size();
        Json ext{{"buffer", 0},
                 {"byteOffset", compressed.size()},
                 {"byteLength", input.size()},
                 {"byteStride", stride},
                 {"count", count},
                 {"mode", mode},
                 {"filter", filter}};
        doc["bufferViews"].push_back({{"buffer", 1},
                                      {"byteOffset", decoded},
                                      {"byteLength", count * stride},
                                      {"extensions", {{"EXT_meshopt_compression", ext}}}});
        compressed.insert(compressed.end(), input.begin(), input.end());
        decoded += count * stride;
        return index;
    }
    GltfSourceBundle bundle() const {
        GltfSourceBundle source;
        source.document = doc;
        source.document["buffers"] =
            Json::array({{{"uri", "geometry.bin"}, {"byteLength", compressed.size()}},
                         {{"byteLength", decoded},
                          {"extensions", {{"EXT_meshopt_compression", {{"fallback", true}}}}}}});
        source.buffers = {{std::make_shared<const Bytes>(compressed), 0, compressed.size()},
                          {nullptr, 0, decoded}};
        return source;
    }
    Json& ext() { return doc["bufferViews"][0]["extensions"]["EXT_meshopt_compression"]; }
};
Fixture triangle() {
    Fixture f;
    const std::array<float, 9> positions{-1, 0, 0, 1, 0, 0, 0, 1, 0};
    f.add(encode(std::as_bytes(std::span(positions)), 12), 3, 12);
    f.doc["bufferViews"][0]["byteStride"] = 12;
    f.doc["accessors"].push_back({{"bufferView", 0},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"},
                                  {"min", {-1, 0, 0}},
                                  {"max", {1, 1, 0}}});
    const Json primitive{{"attributes", {{"POSITION", 0}}}};
    f.doc["meshes"] = Json::array({{{"primitives", Json::array({primitive})}}});
    return f;
}
Bytes indices(std::span<const unsigned> input, bool triangles) {
    Bytes encoded(triangles ? meshopt_encodeIndexBufferBound(input.size(), 100)
                            : meshopt_encodeIndexSequenceBound(input.size(), 100));
    const auto size =
        triangles ? meshopt_encodeIndexBuffer(reinterpret_cast<unsigned char*>(encoded.data()),
                                              encoded.size(), input.data(), input.size())
                  : meshopt_encodeIndexSequence(reinterpret_cast<unsigned char*>(encoded.data()),
                                                encoded.size(), input.data(), input.size());
    require(size != 0, "Fixture index encoding failed");
    encoded.resize(size);
    return encoded;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scratch project");
        const auto root = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(root);
        const auto base = triangle();
        NativeGltfDocument plain(base.bundle());
        require(plain.primitive(0, 0).attributes.at("POSITION").values ==
                    std::vector<float>{-1, 0, 0, 1, 0, 0, 0, 1, 0},
                "Compressed triangle changed positions");
        require(plain.source().buffers.size() == 2 && !plain.source().buffers[1].storage &&
                    plain.source().document["bufferViews"][0]["buffer"] == 1,
                "Decompression mutated captured provenance");
        auto captured = base.bundle();
        atomic_write(root / "geometry.bin",
                     std::string(reinterpret_cast<const char*>(base.compressed.data()),
                                 base.compressed.size()));
        atomic_write(root / "model.gltf", captured.document.dump());
        auto from_disk = capture_gltf_source(root, "model.gltf", {"EXT_meshopt_compression"});
        require(!from_disk.buffers[1].storage, "Placeholder caused eager allocation");
        require(NativeGltfDocument(std::move(from_disk)).primitive(0, 0).vertex_count == 3,
                "Captured placeholder transport did not decode");
        rejects([&] { (void)capture_gltf_source(root, "model.gltf"); }, "Unsupported required");
        auto fallback = base.bundle();
        auto zeros = std::make_shared<const Bytes>(base.decoded, std::byte{0});
        fallback.buffers[1] = {zeros, 0, zeros->size()};
        fallback.document["extensionsRequired"] = Json::array();
        require(NativeGltfDocument(fallback).primitive(0, 0).maximum[0] == 1,
                "Stale ordinary fallback replaced supported compressed data");
        fallback.document["buffers"][1]["extensions"]["EXT_meshopt_compression"]["fallback"] =
            "true";
        rejects([&] { (void)decode_gltf_meshopt(fallback); }, "must be boolean");
        Fixture image;
        const std::array<std::byte, 4> encoded_image{std::byte{1}, std::byte{2}, std::byte{3},
                                                     std::byte{4}};
        image.add(encode(encoded_image, 4), 1, 4);
        auto image_bundle = image.bundle();
        image_bundle.document["images"] =
            Json::array({{{"bufferView", 0}, {"mimeType", "image/png"}}});
        image_bundle.images.push_back({{nullptr, 0, 4}, "image/png"});
        NativeGltfDocument image_document(image_bundle);
        require(
            std::ranges::equal(image_document.encoded_images()[0].encoded.bytes(), encoded_image) &&
                !image_document.source().images[0].encoded.storage,
            "Compressed encoded image transport or source provenance changed");
        auto nonfinite = base;
        std::array<float, 9> nan_positions{};
        nan_positions[0] = std::numeric_limits<float>::quiet_NaN();
        nonfinite.compressed = encode(std::as_bytes(std::span(nan_positions)), 12);
        nonfinite.ext()["byteLength"] = nonfinite.compressed.size();
        rejects([&] { NativeGltfDocument invalid(nonfinite.bundle()); }, "non-finite");
        for (bool triangles : {true, false})
            for (std::size_t width : {2u, 4u}) {
                auto indexed = base;
                const std::array<unsigned, 3> source{0, 1, 2};
                indexed.add(indices(source, triangles), 3, width,
                            triangles ? "TRIANGLES" : "INDICES");
                indexed.doc["accessors"].push_back({{"bufferView", 1},
                                                    {"componentType", width == 2 ? 5123 : 5125},
                                                    {"count", 3},
                                                    {"type", "SCALAR"}});
                indexed.doc["meshes"][0]["primitives"][0]["indices"] = 1;
                require(NativeGltfDocument(indexed.bundle()).primitive(0, 0).indices ==
                            std::vector<std::uint32_t>{0, 1, 2},
                        "Index mode changed winding");
                const std::array<unsigned, 3> bad{0, 1, 20};
                const auto encoded = indices(bad, triangles);
                indexed.compressed.resize(base.compressed.size());
                indexed.compressed.insert(indexed.compressed.end(), encoded.begin(), encoded.end());
                indexed
                    .doc["bufferViews"][1]["extensions"]["EXT_meshopt_compression"]["byteLength"] =
                    encoded.size();
                rejects([&] { (void)NativeGltfDocument(indexed.bundle()).primitive(0, 0); },
                        "index exceeds");
            }
        for (const auto* filter : {"OCTAHEDRAL", "QUATERNION", "EXPONENTIAL"}) {
            const std::array<float, 4> source{0, 0, 0, 1};
            const std::size_t stride = std::string_view(filter) == "OCTAHEDRAL"   ? 4
                                       : std::string_view(filter) == "QUATERNION" ? 8
                                                                                  : 16;
            Bytes encoded(stride);
            if (stride == 4) {
                const std::array<float, 4> normal{0, 0, 1, -1};
                meshopt_encodeFilterOct(encoded.data(), 1, stride, 8, normal.data());
            } else if (stride == 8) {
                meshopt_encodeFilterQuat(encoded.data(), 1, stride, 16, source.data());
            } else {
                meshopt_encodeFilterExp(encoded.data(), 1, stride, 24, source.data(),
                                        meshopt_EncodeExpSeparate);
            }
            Fixture f;
            f.add(encode(encoded, stride), 1, stride, "ATTRIBUTES", filter);
            const auto decoded = decode_gltf_meshopt(f.bundle());
            require(decoded.buffers.back().length == stride, "Filter output size changed");
            if (stride == 4)
                require(decoded.buffers.back().bytes()[2] == std::byte{127},
                        "Oct normal did not decode");
            else if (stride == 8)
                require(decoded.buffers.back().bytes()[6] == std::byte{255} &&
                            decoded.buffers.back().bytes()[7] == std::byte{127},
                        "Quaternion identity did not decode");
            else
                require(decoded.buffers.back().bytes()[15] == std::byte{63},
                        "Exponential value did not decode");
            // Only the minimum malformed marker needed to reject an undefined filter input.
            encoded.assign(stride, std::byte{0});
            if (stride == 16)
                encoded[3] = std::byte{101};
            Fixture invalid;
            invalid.add(encode(encoded, stride), 1, stride, "ATTRIBUTES", filter);
            rejects([&] { (void)decode_gltf_meshopt(invalid.bundle()); }, "filter");
        }
        for (const auto& [field, value] :
             std::vector<std::pair<std::string, Json>>{{"count", UINT64_MAX},
                                                       {"count", 0},
                                                       {"byteStride", 3},
                                                       {"byteOffset", UINT64_MAX},
                                                       {"byteLength", 0},
                                                       {"buffer", 1},
                                                       {"mode", "UNKNOWN"},
                                                       {"filter", "COLOR"}}) {
            auto bad = base;
            bad.ext()[field] = value;
            rejects([&] { (void)decode_gltf_meshopt(bad.bundle()); }, "glTF meshopt");
        }
        for (std::size_t remove : {1u, 4u}) {
            auto bad = base;
            bad.compressed.resize(bad.compressed.size() - remove);
            bad.ext()["byteLength"] = bad.compressed.size();
            rejects([&] { (void)decode_gltf_meshopt(bad.bundle()); }, "decoder rejected");
        }
        auto bad = base;
        bad.compressed[0] = std::byte{0xa1};
        rejects([&] { (void)decode_gltf_meshopt(bad.bundle()); }, "version");
        bad = base;
        bad.doc["bufferViews"][0]["extensions"]["KHR_meshopt_compression"] = Json::object();
        rejects([&] { (void)decode_gltf_meshopt(bad.bundle()); }, "conflicting");
        bad = base;
        bad.doc["extensionsRequired"].push_back("KHR_meshopt_compression");
        rejects([&] { (void)decode_gltf_meshopt(bad.bundle()); }, "not a ratified");
        bad = base;
        bad.doc["bufferViews"][0].erase("extensions");
        rejects([&] { (void)decode_gltf_meshopt(bad.bundle()); }, "uncompressed view");
        bad = base;
        bad.doc["extensionsUsed"] = Json::array();
        rejects([&] { (void)decode_gltf_meshopt(bad.bundle()); }, "inconsistent");
        rejects([&] { (void)decode_gltf_meshopt(base.bundle(), 35); }, "decoded-byte limit");
        std::stop_source cancellation;
        cancellation.request_stop();
        rejects([&] { (void)decode_gltf_meshopt(base.bundle(), 1000, cancellation.get_token()); },
                "cancelled");
        std::cout
            << "Meshopt EXT native decoding, filters, bounds, provenance and rejected RC passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
