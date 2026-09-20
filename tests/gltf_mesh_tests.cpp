#include "gltf_native.hpp"
#include <bit>
#include <iostream>

using namespace forge;
using namespace forge::asset_detail;
namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view expected = {}) {
    try {
        fn();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos)
            return;
        throw std::runtime_error("Unexpected mesh rejection: " + std::string(error.what()));
    }
    throw std::runtime_error("Invalid mesh accepted");
}
struct Fixture {
    Json doc{{"asset", {{"version", "2.0"}}},
             {"accessors", Json::array()},
             {"bufferViews", Json::array()},
             {"materials", Json::array({Json::object()})},
             {"meshes",
              Json::array({{{"primitives", Json::array({{{"attributes", Json::object()}}})}}})}};
    std::vector<std::byte> bytes;
    Json& primitive() { return doc["meshes"][0]["primitives"][0]; }
    std::size_t add(std::vector<double> values, unsigned components, unsigned type = 5126,
                    bool normalized = false, bool vertex = true) {
        const unsigned width = type == 5120 || type == 5121   ? 1
                               : type == 5122 || type == 5123 ? 2
                                                              : 4;
        const unsigned stride = vertex ? (width * components + 3) & ~3u : width * components;
        const auto count = values.size() / components;
        while (bytes.size() % 4)
            bytes.push_back(std::byte{0});
        const auto offset = bytes.size();
        for (std::size_t i = 0; i < count; ++i) {
            for (unsigned j = 0; j < components; ++j) {
                const auto number = values[i * components + j];
                const auto bits = type == 5126 ? std::bit_cast<std::uint32_t>(float(number))
                                               : std::uint32_t(std::int64_t(number));
                for (unsigned b = 0; b < width; ++b)
                    bytes.push_back(std::byte((bits >> (8 * b)) & 255));
            }
            while (bytes.size() - offset < (i + 1) * stride)
                bytes.push_back(std::byte{0});
        }
        Json view{{"buffer", 0},
                  {"byteOffset", offset},
                  {"byteLength", count * stride},
                  {"target", vertex ? 34962 : 34963}};
        if (vertex)
            view["byteStride"] = stride;
        const auto view_id = doc["bufferViews"].size(), id = doc["accessors"].size();
        doc["bufferViews"].push_back(view);
        const std::string shape = components == 1 ? "SCALAR" : "VEC" + std::to_string(components);
        doc["accessors"].push_back({{"bufferView", view_id},
                                    {"componentType", type},
                                    {"count", count},
                                    {"type", shape},
                                    {"normalized", normalized}});
        return id;
    }
    void position(std::vector<double> values) {
        const auto id = add(std::move(values), 3);
        primitive()["attributes"]["POSITION"] = id;
        doc["accessors"][id]["min"] = Json::array({-100, -100, -100});
        doc["accessors"][id]["max"] = Json::array({100, 100, 100});
    }
    NativeMeshPrimitive result() const {
        GltfSourceBundle source;
        source.document = doc;
        source.document["buffers"] = Json::array({{{"byteLength", bytes.size()}}});
        auto storage = std::make_shared<const std::vector<std::byte>>(bytes);
        source.buffers.push_back({storage, 0, bytes.size()});
        return NativeGltfDocument(std::move(source)).primitive(0, 0);
    }
};
Fixture triangle() {
    Fixture result;
    result.position({0, 0, 0, 2, 0, 0, 0, 3, 0});
    return result;
}
} // namespace
int main() {
    try {
        auto base = triangle();
        const auto mesh = base.result();
        require(mesh.indices == std::vector<std::uint32_t>{0, 1, 2} && mesh.vertex_count == 3 &&
                    mesh.minimum == std::array<float, 3>{0, 0, 0} &&
                    mesh.maximum == std::array<float, 3>{2, 3, 0},
                "Nonindexed triangle or computed bounds are incorrect");
        require(!mesh.diagnostics.empty(), "Recomputed source bounds were not diagnosed");
        for (unsigned width : {5121u, 5123u, 5125u}) {
            auto indexed = base;
            indexed.primitive()["indices"] = indexed.add({2, 0, 1}, 1, width, false, false);
            require(indexed.result().indices == std::vector<std::uint32_t>{2, 0, 1},
                    "Valid index width changed vertex correspondence");
        }
        const std::vector<std::vector<std::uint32_t>> expected{
            {0, 1, 2, 3}, {0, 1, 2, 3},       {0, 1, 1, 2, 2, 3, 3, 0}, {0, 1, 1, 2, 2, 3},
            {},           {0, 1, 2, 2, 1, 3}, {0, 1, 2, 0, 2, 3}};
        for (unsigned mode : {0u, 1u, 2u, 3u, 5u, 6u}) {
            Fixture four;
            four.position({0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0});
            four.primitive()["mode"] = mode;
            const auto converted = four.result();
            require(converted.indices == expected[mode],
                    "Topology normalization changed order/winding");
            require(converted.topology == (mode == 0  ? NativePrimitiveTopology::Points
                                           : mode < 4 ? NativePrimitiveTopology::Lines
                                                      : NativePrimitiveTopology::Triangles),
                    "Point/line primitive became a triangle primitive");
        }
        auto streams = base;
        auto& attrs = streams.primitive()["attributes"];
        attrs["NORMAL"] = streams.add({0, 0, 1, 0, 0, 1, 0, 0, 1}, 3);
        attrs["TANGENT"] = streams.add({1, 0, 0, 1, 1, 0, 0, -1, 1, 0, 0, 1}, 4);
        attrs["TEXCOORD_0"] = streams.add({0, 0, 255, 0, 0, 255}, 2, 5121, true);
        attrs["TEXCOORD_1"] = streams.add({0, 0, 1, 0, 0, 1}, 2);
        attrs["COLOR_0"] = streams.add({2, -1, 0.5, 0, 1, 0, 0, 0, 1}, 3);
        attrs["COLOR_1"] = streams.add({2, 0, 0, 1, 0, 0, 0, 0, 1}, 3);
        attrs["_TEMPERATURE"] = streams.add({10, 20, 30}, 1);
        attrs["JOINTS_0"] = streams.add({0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3}, 4, 5121);
        attrs["WEIGHTS_0"] = streams.add({0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.5, 0, 0, 0}, 4);
        attrs["JOINTS_1"] = streams.add({4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7}, 4, 5123);
        attrs["WEIGHTS_1"] = streams.add({0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.5, 0, 0, 0}, 4);
        streams.primitive()["material"] = 0;
        auto decoded = streams.result();
        require(decoded.attributes.at("TEXCOORD_0").values[2] == 1 &&
                    decoded.attributes.at("COLOR_0").values[0] == 1 &&
                    decoded.attributes.at("COLOR_0").values[1] == 0 &&
                    decoded.attributes.at("COLOR_1").values[0] == 2 &&
                    decoded.attributes.contains("_TEMPERATURE") && !decoded.diagnostics.empty(),
                "Attribute normalization, color policy or custom preservation failed");
        require(decoded.integer_attributes.at("JOINTS_1").values[3] == 7 &&
                    decoded.attributes.contains("WEIGHTS_1") && decoded.material == 0,
                "Additional skin inputs or material slot were lost");
        const auto delta = streams.add({0, 0, 1, 0, 0, 1, 0, 0, 1}, 3);
        streams.doc["accessors"][delta]["min"] = Json::array({0, 0, 1});
        streams.doc["accessors"][delta]["max"] = Json::array({0, 0, 1});
        const auto uv_delta = streams.add({0, 0, 1, 0, 0, 1}, 2);
        streams.primitive()["targets"] =
            Json::array({{{"POSITION", delta}, {"TEXCOORD_0", uv_delta}}});
        streams.doc["meshes"][0]["weights"] = Json::array({0.5});
        auto morph = streams.result();
        require(morph.morph_targets.size() == 1 &&
                    morph.morph_targets[0].at("POSITION").values[2] == 1 &&
                    morph.morph_targets[0].contains("TEXCOORD_0"),
                "Morph streams were dropped");
        // Four-influence policy is explicit; palette entries retain source joint order.
        auto skin = prepare_gltf_skin_influences(decoded, 8, ExcessSkinInfluences::Reject);
        require(skin.palette == std::vector<std::uint32_t>{0, 4} &&
                    skin.vertices[0].weights[0] == 0.5f && skin.vertices[0].joints[1] == 1,
                "Draw palette or multi-set influences changed");
        auto many = decoded;
        auto& weights0 = many.attributes.at("WEIGHTS_0").values;
        auto& weights1 = many.attributes.at("WEIGHTS_1").values;
        for (auto& value : weights0)
            value = 0.125f;
        for (auto& value : weights1)
            value = 0.125f;
        rejects([&] { prepare_gltf_skin_influences(many, 8, ExcessSkinInfluences::Reject); },
                "four influences");
        auto reduced = prepare_gltf_skin_influences(many, 8, ExcessSkinInfluences::ReduceToFour);
        require(reduced.reduced_vertices == 3 &&
                    reduced.palette == std::vector<std::uint32_t>{0, 1, 2, 3} &&
                    reduced.vertices[0].weights == std::array<float, 4>{0.25f, 0.25f, 0.25f, 0.25f},
                "Explicit top-four tie-breaking or renormalization is incorrect");
        auto broken_skin = decoded;
        broken_skin.integer_attributes.at("JOINTS_1").values[0] = 0;
        rejects([&] { prepare_gltf_skin_influences(broken_skin, 8, ExcessSkinInfluences::Reject); },
                "duplicate nonzero");
        broken_skin = decoded;
        broken_skin.integer_attributes.at("JOINTS_1").values[1] = 8;
        rejects([&] { prepare_gltf_skin_influences(broken_skin, 8, ExcessSkinInfluences::Reject); },
                "zero-weight slots");
        broken_skin = decoded;
        for (auto& value : broken_skin.attributes.at("WEIGHTS_0").values)
            value = 0;
        for (auto& value : broken_skin.attributes.at("WEIGHTS_1").values)
            value = 0;
        rejects([&] { prepare_gltf_skin_influences(broken_skin, 8, ExcessSkinInfluences::Reject); },
                "zero");
        broken_skin = decoded;
        broken_skin.attributes.at("WEIGHTS_0").values[0] = 0.25f;
        auto renormalized =
            prepare_gltf_skin_influences(broken_skin, 8, ExcessSkinInfluences::Reject);
        require(renormalized.renormalized_vertices == 1 &&
                    renormalized.vertices[0].weights[0] > 0.66f,
                "Non-unit float weights were not diagnosed and normalized");
        NativeMeshPrimitive large_palette;
        large_palette.vertex_count = 257;
        large_palette.integer_attributes["JOINTS_0"] = {257, 4,
                                                        std::vector<std::uint32_t>(257 * 4)};
        large_palette.attributes["WEIGHTS_0"] = {257, 4, std::vector<float>(257 * 4)};
        for (unsigned i = 0; i < 257; ++i) {
            large_palette.integer_attributes["JOINTS_0"].values[i * 4] = i;
            large_palette.attributes["WEIGHTS_0"].values[i * 4] = 1;
        }
        rejects(
            [&] { prepare_gltf_skin_influences(large_palette, 257, ExcessSkinInfluences::Reject); },
            "256 palette");
        auto quantized = base;
        quantized.primitive()["attributes"]["JOINTS_0"] =
            quantized.add({0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0}, 4, 5121);
        quantized.primitive()["attributes"]["WEIGHTS_0"] =
            quantized.add({128, 127, 0, 0, 128, 127, 0, 0, 128, 127, 0, 0}, 4, 5121, true);
        require(prepare_gltf_skin_influences(quantized.result(), 2, ExcessSkinInfluences::Reject)
                        .vertices.size() == 3,
                "Exact quantized sum rejected");
        quantized.primitive()["attributes"]["WEIGHTS_0"] =
            quantized.add({128, 126, 0, 0, 128, 127, 0, 0, 128, 127, 0, 0}, 4, 5121, true);
        rejects([&] { quantized.result(); }, "quantized skin weights");
        auto bad = base;
        bad.primitive()["indices"] = bad.add({0, 1, 3}, 1, 5121, false, false);
        rejects([&] { (void)bad.result(); }, "exceeds vertex");
        bad = base;
        bad.primitive()["indices"] = bad.add({0, 1, 255}, 1, 5121, false, false);
        rejects([&] { (void)bad.result(); }, "restart");
        bad = base;
        bad.primitive()["mode"] = 1;
        rejects([&] { (void)bad.result(); }, "topology");
        bad = base;
        bad.primitive()["mode"] = 4.5;
        rejects([&] { (void)bad.result(); });
        bad = base;
        bad.primitive()["attributes"]["NORMAL"] = bad.add({0, 0, 0, 0, 0, 1, 0, 0, 1}, 3);
        rejects([&] { (void)bad.result(); }, "normalized");
        bad = base;
        bad.primitive()["attributes"]["TEXCOORD_1"] = bad.add({0, 0, 1, 0, 0, 1}, 2);
        rejects([&] { (void)bad.result(); }, "consecutive");
        bad = base;
        bad.primitive()["attributes"]["TEXCOORD_01"] = bad.add({0, 0, 1, 0, 0, 1}, 2);
        rejects([&] { (void)bad.result(); }, "set index");
        bad = base;
        bad.primitive()["attributes"]["_SHORT"] = bad.add({0, 1}, 1);
        rejects([&] { (void)bad.result(); }, "counts disagree");
        bad = base;
        bad.primitive()["material"] = 1;
        rejects([&] { (void)bad.result(); }, "material");
        bad = streams;
        bad.primitive()["attributes"].erase("WEIGHTS_1");
        rejects([&] { (void)bad.result(); }, "joint and weight");
        bad = streams;
        bad.doc["meshes"][0]["weights"] = Json::array({0, 0});
        rejects([&] { (void)bad.result(); }, "weight count");
        bad = streams;
        auto extra_primitive = bad.primitive();
        extra_primitive.erase("targets");
        bad.doc["meshes"][0]["primitives"].push_back(extra_primitive);
        rejects([&] { (void)bad.result(); }, "inconsistent morph");
        std::cout << "Native mesh attributes, index widths, topology, computed bounds and morph "
                     "inputs passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
