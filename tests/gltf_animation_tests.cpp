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
        throw std::runtime_error("Unexpected animation rejection: " + std::string(error.what()));
    }
    throw std::runtime_error("Invalid animation accepted");
}
struct Fixture {
    Json doc{{"asset", {{"version", "2.0"}}},
             {"nodes", Json::array({Json::object()})},
             {"accessors", Json::array()},
             {"bufferViews", Json::array()},
             {"animations", Json::array()}};
    std::vector<std::byte> bytes;
    std::size_t add(const std::vector<float>& values, unsigned components) {
        const auto offset = bytes.size();
        for (auto value : values) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned b = 0; b < 4; ++b)
                bytes.push_back(std::byte((bits >> (b * 8)) & 255));
        }
        const auto view = doc["bufferViews"].size(), accessor = doc["accessors"].size();
        doc["bufferViews"].push_back(
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", values.size() * 4}});
        const auto shape = components == 1    ? "SCALAR"
                           : components == 16 ? "MAT4"
                                              : "VEC" + std::to_string(components);
        doc["accessors"].push_back({{"bufferView", view},
                                    {"count", values.size() / components},
                                    {"type", shape},
                                    {"componentType", 5126}});
        return accessor;
    }
    void channel(const std::string& path, std::vector<float> values, unsigned components,
                 const std::string& interpolation = "LINEAR", std::vector<float> times = {0, 1}) {
        const auto input = add(times, 1), output = add(values, components);
        doc["accessors"][input]["min"] = {times.front()};
        doc["accessors"][input]["max"] = {times.back()};
        doc["animations"].push_back(
            {{"samplers",
              Json::array(
                  {{{"input", input}, {"output", output}, {"interpolation", interpolation}}})},
             {"channels",
              Json::array({{{"sampler", 0}, {"target", {{"node", 0}, {"path", path}}}}})}});
    }
    std::unique_ptr<NativeGltfDocument> native() const {
        GltfSourceBundle source;
        source.document = doc;
        if (!bytes.empty()) {
            source.document["buffers"] = Json::array({{{"byteLength", bytes.size()}}});
            auto storage = std::make_shared<const std::vector<std::byte>>(bytes);
            source.buffers.push_back({storage, 0, bytes.size()});
        }
        return std::make_unique<NativeGltfDocument>(std::move(source));
    }
    NativeAnimationClip result() const { return native()->animation(0); }
};
} // namespace
int main() {
    try {
        Fixture linear;
        linear.channel("translation", {0, 1, 2, 3, 4, 5}, 3);
        const auto clip = linear.result();
        require(clip.tracks.size() == 1 && clip.duration == 1 && clip.tracks[0].components == 3 &&
                    *clip.tracks[0].values == std::vector<float>{0, 1, 2, 3, 4, 5},
                "Translation data changed");
        auto shared = linear;
        shared.doc["nodes"].push_back(Json::object());
        auto channel = shared.doc["animations"][0]["channels"][0];
        channel["target"]["node"] = 1;
        shared.doc["animations"][0]["channels"].push_back(channel);
        const auto sharing = shared.result();
        require(sharing.tracks[0].times == sharing.tracks[1].times &&
                    sharing.tracks[0].values == sharing.tracks[1].values,
                "Shared samplers duplicate decoded payload");
        Fixture step;
        step.channel("scale", {1, 1, 1, -1, 0, 2}, 3, "STEP", {2, 4});
        require(step.result().tracks[0].interpolation == NativeAnimationInterpolation::Step &&
                    step.result().duration == 4,
                "Step mode or source signed/zero scale changed");
        Fixture cubic;
        cubic.channel("rotation",
                      {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 0, 0, 0, 0, 0, 0}, 4,
                      "CUBICSPLINE");
        const auto cubic_clip = cubic.result();
        require(cubic_clip.tracks[0].values->size() == 24 &&
                    (*cubic_clip.tracks[0].values)[12] == 6 &&
                    cubic_clip.tracks[0].interpolation == NativeAnimationInterpolation::CubicSpline,
                "Cubic quaternion tangents changed or were incorrectly normalized");
        Fixture morph;
        const auto position = morph.add({0, 0, 0, 1, 0, 0, 0, 1, 0}, 3);
        morph.doc["accessors"][position]["min"] = {0, 0, 0};
        morph.doc["accessors"][position]["max"] = {1, 1, 0};
        Json morph_primitive{
            {"attributes", {{"POSITION", position}}},
            {"targets", Json::array({{{"POSITION", position}}, {{"POSITION", position}}})}};
        morph.doc["meshes"] = Json::array({{{"primitives", Json::array({morph_primitive})}}});
        morph.doc["nodes"][0]["mesh"] = 0;
        morph.channel("weights", {0, 0, 1, 0}, 1);
        require(morph.result().tracks[0].components == 2 &&
                    morph.result().tracks[0].values->size() == 4,
                "Morph output dimensions are incorrect");
        auto bad = linear;
        bad.doc["animations"][0]["channels"].push_back(bad.doc["animations"][0]["channels"][0]);
        rejects([&] { bad.result(); }, "duplicate node/path");
        bad = linear;
        bad.doc["nodes"][0]["matrix"] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        rejects([&] { bad.result(); }, "matrix-authored");
        bad = linear;
        bad.doc["animations"][0]["channels"][0]["target"]["node"] = 999999999999ull;
        rejects([&] { bad.result(); }, "node index");
        bad = linear;
        bad.doc["animations"][0]["samplers"][0]["input"] = 999999999999ull;
        rejects([&] { bad.result(); }, "accessor index");
        Fixture repeated;
        repeated.channel("translation", {0, 0, 0, 1, 1, 1}, 3, "LINEAR", {1, 1});
        rejects([&] { repeated.result(); }, "strictly increasing");
        Fixture negative;
        negative.channel("translation", {0, 0, 0, 1, 1, 1}, 3, "LINEAR", {-1, 1});
        rejects([&] { negative.result(); }, "nonnegative");
        Fixture too_short;
        too_short.channel("translation", {0, 0, 0, 1, 1, 1, 0, 0, 0}, 3, "CUBICSPLINE", {0});
        rejects([&] { too_short.result(); }, "insufficient");
        Fixture wrong_count;
        wrong_count.channel("rotation", {0, 0, 0, 1}, 4);
        rejects([&] { wrong_count.result(); }, "key count");
        Fixture wrong_rotation;
        wrong_rotation.channel("rotation", {0, 0, 0, 0, 0, 0, 0, 1}, 4);
        rejects([&] { wrong_rotation.result(); }, "unit quaternion");
        bad = linear;
        bad.doc["animations"][0]["channels"][0]["target"]["path"] = "weights";
        rejects([&] { bad.result(); }, "mesh morph");
        bad = linear;
        bad.doc["accessors"][0]["max"] = {20};
        rejects([&] { bad.result(); }, "bounds disagree");
        bad = linear;
        bad.doc["bufferViews"][1]["target"] = 34962;
        rejects([&] { bad.result(); }, "vertex/index");
        bad = linear;
        bad.doc["animations"][0]["channels"][0]["target"].erase("node");
        const auto ignored = bad.result();
        require(ignored.tracks.empty() && !ignored.diagnostics.empty(),
                "Unbound channel was silently advertised as animated");
        Fixture skin;
        skin.doc["nodes"] = Json::array({{{"children", {1}}}, Json::object()});
        skin.doc["skins"] = Json::array({{{"joints", {0, 1}}}});
        auto binds = skin.native()->inverse_bind_matrices(0);
        require(binds.size() == 2 && binds[1][15] == 1, "Missing inverse binds are not identity");
        const std::vector<float> matrices{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, -3, -4, 1,
                                          2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5,  6,  7,  1};
        skin.doc["skins"][0]["inverseBindMatrices"] = skin.add(matrices, 16);
        binds = skin.native()->inverse_bind_matrices(0);
        require(binds[0][12] == -2 && binds[1][5] == 3 && binds[1][14] == 7,
                "Inverse bind order, basis or translation changed");
        skin.bytes[3 * 4 + 3] = std::byte{0x3f};
        rejects([&] { skin.native()->inverse_bind_matrices(0); }, "must be affine");
        std::cout << "glTF TRS/morph animation admission, shared samples and inverse bind "
                     "decoding passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
