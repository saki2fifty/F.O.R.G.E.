#include "gltf_draco.hpp"
#include "gltf_native.hpp"
#include <algorithm>
#include <cstring>
#include <draco/compression/encode.h>
#include <draco/mesh/triangle_soup_mesh_builder.h>
#include <forge/scene.hpp>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::byte>;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view expected) {
    try {
        fn();
    } catch (const std::exception& e) {
        if (std::string_view(e.what()).find(expected) != std::string_view::npos)
            return;
        throw std::runtime_error("Unexpected rejection: " + std::string(e.what()));
    }
    throw std::runtime_error("Invalid Draco fixture accepted");
}
GltfSourceBundle fixture(unsigned face_count = 1) {
    draco::TriangleSoupMeshBuilder builder;
    builder.Start(static_cast<int>(face_count));
    const auto pos = builder.AddAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32);
    const auto col =
        builder.AddAttribute(draco::GeometryAttribute::COLOR, 3, draco::DT_UINT8, true);
    for (unsigned f = 0; f < face_count; ++f) {
        const float x = float(f) * 3;
        const float a[]{x - 1, 0, 0}, b[]{x + 1, 0, 0}, c[]{x, 1, 0};
        const std::uint8_t ca[]{255, 0, 0}, cb[]{0, 255, 0}, cc[]{0, 0, 255};
        builder.SetAttributeValuesForFace(pos, draco::FaceIndex(f), a, b, c);
        builder.SetAttributeValuesForFace(col, draco::FaceIndex(f), ca, cb, cc);
    }
    builder.SetAttributeUniqueId(pos, 73);
    builder.SetAttributeUniqueId(col, 91);
    auto mesh = builder.Finalize();
    require(bool(mesh), "Native fixture builder failed");
    draco::Encoder encoder;
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 14);
    draco::EncoderBuffer output;
    require(encoder.EncodeMeshToBuffer(*mesh, &output).ok(), "Native fixture encoder failed");
    auto bytes = std::make_shared<Bytes>(output.size());
    std::memcpy(bytes->data(), output.data(), output.size());
    GltfSourceBundle source;
    source.buffers = {{bytes, 0, bytes->size()}};
    const Json primitive{
        {"attributes", {{"POSITION", 0}, {"COLOR_0", 1}}},
        {"indices", 2},
        {"extensions",
         {{"KHR_draco_mesh_compression",
           {{"bufferView", 0}, {"attributes", {{"POSITION", 73}, {"COLOR_0", 91}}}}}}}};
    source.document = {
        {"asset", {{"version", "2.0"}}},
        {"extensionsUsed", {"KHR_draco_mesh_compression"}},
        {"extensionsRequired", {"KHR_draco_mesh_compression"}},
        {"buffers", Json::array({{{"byteLength", bytes->size()}, {"uri", "geometry.drc"}}})},
        {"bufferViews", Json::array({{{"buffer", 0}, {"byteLength", bytes->size()}}})},
        {"accessors",
         Json::array({{{"componentType", 5126},
                       {"type", "VEC3"},
                       {"count", face_count * 3},
                       {"min", {-1, 0, 0}},
                       {"max", {1, 1, 0}}},
                      {{"componentType", 5121},
                       {"type", "VEC3"},
                       {"count", face_count * 3},
                       {"normalized", true}},
                      {{"componentType", 5123}, {"type", "SCALAR"}, {"count", face_count * 3}}})},
        {"meshes", Json::array({{{"primitives", Json::array({primitive})}}})}};
    return source;
}
Json& primitive(GltfSourceBundle& source) { return source.document["meshes"][0]["primitives"][0]; }
void changed_bytes(GltfSourceBundle& source, Bytes bytes) {
    auto data = std::make_shared<Bytes>(std::move(bytes));
    source.buffers[0] = {data, 0, data->size()};
    source.document["buffers"][0]["byteLength"] = data->size();
    source.document["bufferViews"][0]["byteLength"] = data->size();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected fixture directory and exact upstream fixtures");
        const auto official_root = std::filesystem::path(argv[2]);
        NativeGltfDocument official(capture_gltf_source(
            official_root, "Box/glTF_Binary/Box_Draco.glb", {"KHR_draco_mesh_compression"}));
        const auto box = official.primitive(0, 0);
        require(box.vertex_count == 24 && box.indices.size() == 36 &&
                    box.attributes.contains("NORMAL"),
                "Official Draco Box geometry differs");
        require(cook_gltf_mesh(official, 0).lods.at(0).parts.size() == 1,
                "Official Draco Box cook failed");
        const auto base = fixture();
        NativeGltfDocument document(base);
        const auto mesh = document.primitive(0, 0);
        require(mesh.vertex_count == 3 && mesh.indices.size() == 3, "Native geometry missing");
        const auto& positions = mesh.attributes.at("POSITION").values;
        const auto& colors = mesh.attributes.at("COLOR_0").values;
        for (std::size_t v = 0; v < 3; ++v) {
            const auto channel = positions[v * 3] < -0.5f ? 0u : positions[v * 3] > 0.5f ? 1u : 2u;
            require(colors[v * 3 + channel] == 1.f,
                    "Vertex-color mapping lost after native decode");
        }
        require(document.source().document == base.document &&
                    document.source().buffers.size() == 1,
                "Decoder changed captured provenance");
        const auto normal = decode_gltf_draco(base);
        require(normal.document == decode_gltf_draco(base).document,
                "Transport metadata nondeterministic");
        for (std::size_t i = 1; i < normal.buffers.size(); ++i)
            require(*normal.buffers[i].storage == *decode_gltf_draco(base).buffers[i].storage,
                    "Transport bytes nondeterministic");
        auto large = fixture(86);
        large.document["accessors"][2]["componentType"] = 5121;
        rejects([&] { (void)decode_gltf_draco(large); }, "face index exceeds");
        auto variant = normal;
        primitive(variant)["extensions"] =
            base.document["meshes"][0]["primitives"][0]["extensions"];
        variant.document["extensionsRequired"] = Json::array();
        require(NativeGltfDocument(variant).primitive(0, 0).indices == mesh.indices,
                "Optional compression with valid fallback failed");
        variant = base;
        primitive(variant).erase("indices");
        require(NativeGltfDocument(variant).primitive(0, 0).indices.size() == 3,
                "Nonindexed compressed geometry lost native connectivity");
        variant = base;
        primitive(variant)["mode"] = 5;
        require(NativeGltfDocument(variant).primitive(0, 0).indices == mesh.indices,
                "Strip native faces were reinterpreted as strip data");
        // A reused accessor and compressed view must agree, including unique IDs.
        variant = base;
        variant.document["meshes"][0]["primitives"].push_back(primitive(variant));
        require(NativeGltfDocument(variant).primitive(0, 1).indices == mesh.indices,
                "Shared decoded view failed");
        // Normative decode ignores fallback ranges for compressed accessors.
        variant = base;
        variant.document["accessors"][0]["bufferView"] = 999;
        variant.document["accessors"][0]["byteOffset"] = 999;
        require(NativeGltfDocument(variant).primitive(0, 0).vertex_count == 3,
                "Fallback range overrode Draco geometry");
        // Ordinary attributes, sparse patches and morph targets remain separate
        // from Draco's compressed attribute map.
        variant = base;
        const std::array<float, 9> delta{0, 0, 1, 0, 0, 1, 0, 0, 1};
        auto extra = std::make_shared<Bytes>(sizeof(delta));
        std::memcpy(extra->data(), delta.data(), sizeof(delta));
        variant.buffers.push_back({extra, 0, extra->size()});
        variant.document["buffers"].push_back({{"byteLength", extra->size()}});
        variant.document["bufferViews"].push_back({{"buffer", 1}, {"byteLength", extra->size()}});
        variant.document["accessors"].push_back({{"bufferView", 1},
                                                 {"componentType", 5126},
                                                 {"type", "VEC3"},
                                                 {"count", 3},
                                                 {"min", {0, 0, 1}},
                                                 {"max", {0, 0, 1}}});
        primitive(variant)["attributes"]["_CUSTOM"] = 3;
        primitive(variant)["targets"] = Json::array({{{"POSITION", 3}}});
        auto enriched = NativeGltfDocument(variant).primitive(0, 0);
        require(enriched.attributes.at("_CUSTOM").values ==
                        std::vector<float>(delta.begin(), delta.end()) &&
                    enriched.morph_targets.at(0).at("POSITION").values ==
                        enriched.attributes.at("_CUSTOM").values,
                "Additional attributes or morph values were lost");
        // Sparse override one decoded color with a separately captured patch.
        variant = base;
        auto patch = std::make_shared<Bytes>(
            Bytes{std::byte{1}, std::byte{255}, std::byte{255}, std::byte{255}});
        variant.buffers.push_back({patch, 0, patch->size()});
        variant.document["buffers"].push_back({{"byteLength", patch->size()}});
        variant.document["bufferViews"].push_back({{"buffer", 1}, {"byteLength", 1}});
        variant.document["bufferViews"].push_back(
            {{"buffer", 1}, {"byteOffset", 1}, {"byteLength", 3}});
        variant.document["accessors"][1]["sparse"] = {
            {"count", 1},
            {"indices", {{"bufferView", 1}, {"componentType", 5121}}},
            {"values", {{"bufferView", 2}}}};
        const auto patched =
            NativeGltfDocument(variant).primitive(0, 0).attributes.at("COLOR_0").values;
        require(patched[3] == 1.f && patched[4] == 1.f && patched[5] == 1.f,
                "Sparse patch over decoded colors was lost");
        const auto root = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(root);
        const auto bytes = base.buffers[0].bytes();
        atomic_write(root / "geometry.drc",
                     std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        atomic_write(root / "model.gltf", base.document.dump());
        require(NativeGltfDocument(
                    capture_gltf_source(root, "model.gltf", {"KHR_draco_mesh_compression"}))
                        .primitive(0, 0)
                        .vertex_count == 3,
                "Disk capture did not reach native decoder");
        rejects([&] { (void)capture_gltf_source(root, "model.gltf"); }, "Unsupported required");
        const auto check = [&](auto edit, std::string_view expected) {
            auto broken = base;
            edit(broken);
            rejects([&] { (void)NativeGltfDocument(broken); }, expected);
        };
        check([](auto& s) { s.document["extensionsUsed"] = Json::array(); }, "undeclared");
        check([](auto& s) { primitive(s)["mode"] = 1; }, "topology");
        check([](auto& s) { s.document["extensionsRequired"] = Json::array(); },
              "without fallback");
        check(
            [](auto& s) {
                primitive(s)["extensions"]["KHR_draco_mesh_compression"]["bufferView"] = 999;
            },
            "view");
        check(
            [](auto& s) {
                primitive(s)["extensions"]["KHR_draco_mesh_compression"]["attributes"]["NORMAL"] =
                    2;
            },
            "subset");
        check(
            [](auto& s) {
                primitive(s)["extensions"]["KHR_draco_mesh_compression"]["attributes"]["POSITION"] =
                    999;
            },
            "differs");
        check([](auto& s) { s.document["accessors"][0]["count"] = 2; }, "differs");
        check([](auto& s) { s.document["accessors"][0]["count"] = UINT32_MAX; }, "limit");
        check([](auto& s) { s.document["accessors"][0]["componentType"] = 5123; }, "differs");
        check([](auto& s) { s.document["accessors"][0]["type"] = "VEC4"; }, "differs");
        check([](auto& s) { s.document["accessors"][1]["normalized"] = false; }, "differs");
        check([](auto& s) { s.document["accessors"][2]["count"] = 4; }, "differs");
        check([](auto& s) { s.document["accessors"][2]["normalized"] = true; }, "differs");
        check([](auto& s) { s.document["bufferViews"][0]["byteOffset"] = 1; }, "exceeds buffer");
        check([](auto& s) { s.document["bufferViews"][0]["byteStride"] = 4; }, "range");
        for (const auto count : {std::size_t(1), bytes.size() / 2, bytes.size() - 1}) {
            variant = base;
            changed_bytes(variant, Bytes(bytes.begin(), bytes.begin() + count));
            rejects([&] { (void)decode_gltf_draco(variant); }, "decoder rejected");
        }
        variant = base;
        auto corrupt = Bytes(bytes.begin(), bytes.end());
        corrupt[0] = std::byte{0};
        changed_bytes(variant, corrupt);
        rejects([&] { (void)decode_gltf_draco(variant); }, "decoder rejected");
        variant = base;
        corrupt = Bytes(bytes.begin(), bytes.end());
        corrupt.push_back(std::byte{1});
        changed_bytes(variant, corrupt);
        rejects([&] { (void)decode_gltf_draco(variant); }, "trailing");
        variant = base;
        corrupt = Bytes(bytes.begin(), bytes.end());
        corrupt.resize((corrupt.size() + 3) & ~std::size_t(3), std::byte{0});
        changed_bytes(variant, corrupt);
        require(NativeGltfDocument(variant).primitive(0, 0).indices == mesh.indices,
                "Valid native writer padding rejected");
        corrupt.insert(corrupt.end(), 4, std::byte{0});
        changed_bytes(variant, corrupt);
        rejects([&] { (void)decode_gltf_draco(variant); }, "trailing");
        rejects([&] { (void)decode_gltf_draco(base, 8); }, "limit");
        std::stop_source cancelled;
        cancelled.request_stop();
        rejects([&] { (void)decode_gltf_draco(base, 512 * 1024 * 1024, cancelled.get_token()); },
                "cancelled");
        std::cout << "Draco native geometry, mapping, transport and rejection checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
