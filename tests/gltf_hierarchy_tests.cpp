#include "gltf_native.hpp"
#include "gltf_transform.hpp"
#include <cmath>
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
        throw std::runtime_error("Unexpected hierarchy rejection: " + std::string(error.what()));
    }
    throw std::runtime_error("Invalid hierarchy accepted");
}
NativeGltfHierarchy hierarchy(const Json& document) {
    GltfSourceBundle source;
    source.document = document;
    return validate_gltf_hierarchy(source);
}
Json skeleton() {
    return {
        {"nodes",
         Json::array(
             {{{"children", {1, 2}}}, Json::object(), Json::object(), {{"mesh", 0}, {"skin", 0}}})},
        {"meshes",
         Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}}}})}}})},
        {"skins", Json::array({{{"joints", {1, 2}}, {"skeleton", 0}}})},
        {"scenes", Json::array({{{"nodes", {0, 3}}}})},
        {"scene", 0}};
}
} // namespace
int main() {
    try {
        auto doc = skeleton();
        const auto valid = hierarchy(doc);
        require(valid.nodes[1].parent == 0 && valid.skins[0].common_root == 0 &&
                    valid.skins[0].joints == std::vector<std::size_t>{1, 2} &&
                    valid.default_scene == 0 && valid.nodes[3].root == 3,
                "Hierarchy or ordered joint identity changed");
        doc["scenes"].push_back(doc["scenes"][0]);
        require(hierarchy(doc).scenes.size() == 2, "Shared scene membership rejected");
        doc["nodes"][1]["translation"] = {2, 3, 4};
        doc["nodes"][1]["rotation"] = {0, 0, std::sqrt(0.5), std::sqrt(0.5)};
        doc["nodes"][1]["scale"] = {-2, 3, 0};
        const auto matrix = hierarchy(doc).nodes[1].matrix;
        require(std::abs(matrix[1] + 2) < 1e-9 && std::abs(matrix[4] + 3) < 1e-9 &&
                    matrix[10] == 0 && matrix[12] == 2 && matrix[13] == 3 && matrix[14] == 4,
                "Source TRS reflection, zero scale, orientation or translation was lost");
        const Json exact_trs = {{"translation", {1e100, -2.0, 3.0}},
                                {"rotation", {0.0, 0.0, std::sqrt(.5), std::sqrt(.5)}},
                                {"scale", {-1e-100, 0.0, 2e100}}};
        const auto canonical = canonical_gltf_trs(exact_trs);
        require(canonical.at("translation") == exact_trs.at("translation") &&
                    canonical.at("scale") == exact_trs.at("scale") &&
                    std::abs(canonical.at("rotation")[2].get<double>() - std::sqrt(.5)) < 1e-15,
                "Static source TRS adopted ECS/Ozz bounds or lost zero-scale rotation");
        const auto columns = gltf_node_matrix(exact_trs);
        // A non-singular matrix with scales outside either consumer's profile is
        // still valid immutable source data; later consumers enforce their bounds.
        auto matrix_trs = exact_trs;
        matrix_trs["scale"][1] = -3e-100;
        const auto source_matrix = gltf_node_matrix(matrix_trs);
        const auto decomposed = canonical_gltf_trs({{"matrix", source_matrix}});
        const auto rebuilt = gltf_node_matrix(decomposed);
        for (unsigned c = 0; c < 3; ++c) {
            const auto scale = matrix_trs.at("scale")[c].get<double>();
            for (unsigned r = 0; r < 3; ++r)
                require(std::abs(rebuilt[c * 4 + r] / scale - source_matrix[c * 4 + r] / scale) <
                            2e-6,
                        "Source matrix canonical TRS reconstruction changed its basis");
        }
        require(rebuilt[12] == 1e100 && columns[4] == 0 && columns[5] == 0 && columns[6] == 0,
                "Canonical TRS changed large translation or zero-scale column");
        rejects([&] { canonical_ozz_rest(exact_trs); }, "Ozz profile");
        auto bad = skeleton();
        bad["nodes"][1]["children"] = {0};
        rejects([&] { hierarchy(bad); }, "cycle");
        bad = skeleton();
        bad["nodes"][0]["children"] = {1, 1};
        rejects([&] { hierarchy(bad); }, "duplicate");
        bad = skeleton();
        bad["nodes"][2]["children"] = {1};
        rejects([&] { hierarchy(bad); }, "multiple");
        bad = skeleton();
        bad["nodes"][0]["children"] = {999999999999ull};
        rejects([&] { hierarchy(bad); }, "index");
        bad = skeleton();
        bad["nodes"][1]["rotation"] = {0, 0, 0, 0};
        rejects([&] { hierarchy(bad); }, "quaternion");
        bad = skeleton();
        const Json affine = {1, 0, 0, 0, 0, 2, 0, 0, 0, 0, -3, 0, 4, 5, 6, 1};
        bad["nodes"][1]["matrix"] = affine;
        require(hierarchy(bad).nodes[1].matrix[10] == -3, "Matrix reflection was changed");
        bad["nodes"][1]["translation"] = {0, 0, 0};
        rejects([&] { hierarchy(bad); }, "both matrix");
        bad["nodes"][1].erase("translation");
        bad["nodes"][1]["matrix"][4] = 0.2;
        rejects([&] { hierarchy(bad); }, "shear");
        bad["nodes"][1]["matrix"] = affine;
        bad["nodes"][1]["matrix"][3] = 1;
        rejects([&] { hierarchy(bad); }, "affine");
        bad["nodes"][1]["matrix"] = affine;
        bad["nodes"][1]["matrix"][0] = 0;
        rejects([&] { hierarchy(bad); }, "singular");
        bad = skeleton();
        bad["nodes"][3].erase("mesh");
        rejects([&] { hierarchy(bad); }, "requires a mesh");
        bad = skeleton();
        bad["skins"][0]["joints"] = {1, 1};
        rejects([&] { hierarchy(bad); }, "duplicate joints");
        bad = skeleton();
        bad["skins"][0]["joints"] = {1, 3};
        rejects([&] { hierarchy(bad); }, "common root");
        bad = skeleton();
        bad["skins"][0]["skeleton"] = 1;
        rejects([&] { hierarchy(bad); }, "ancestor");
        bad = skeleton();
        bad["scenes"][0]["nodes"] = {3};
        rejects([&] { hierarchy(bad); }, "missing from mesh scene");
        bad = skeleton();
        bad["scenes"][0]["nodes"] = {0, 1, 3};
        rejects([&] { hierarchy(bad); }, "non-root");
        bad = skeleton();
        bad["scene"] = 2;
        rejects([&] { hierarchy(bad); }, "default scene");
        bad = skeleton();
        bad["skins"][0]["inverseBindMatrices"] = 0;
        bad["accessors"] = Json::array({{{"type", "MAT4"}, {"componentType", 5126}, {"count", 3}}});
        require(hierarchy(bad).skins[0].inverse_bind_accessor == 0,
                "Larger-than-joint-count inverse bind accessor rejected");
        bad["accessors"][0]["count"] = 1;
        rejects([&] { hierarchy(bad); }, "shape/count/type");
        bad["accessors"][0]["count"] = 2;
        bad["accessors"][0]["bufferView"] = 0;
        bad["bufferViews"] = Json::array({{{"target", 34962}}});
        rejects([&] { hierarchy(bad); }, "vertex/index");
        auto morph = skeleton();
        morph["meshes"][0]["primitives"][0]["targets"] =
            Json::array({Json::object(), Json::object()});
        morph["meshes"][0]["weights"] = {0.1, -0.2};
        require(hierarchy(morph).nodes[3].morph_weights == std::vector<double>{0.1, -0.2},
                "Mesh morph defaults lost");
        morph["nodes"][3]["weights"] = {0.7, 2};
        require(hierarchy(morph).nodes[3].morph_weights == std::vector<double>{0.7, 2},
                "Node morph defaults did not override mesh defaults");
        morph["nodes"][3]["weights"] = {0.7};
        rejects([&] { hierarchy(morph); }, "weight count");
        auto camera = skeleton();
        camera["cameras"] = Json::array(
            {{{"type", "perspective"}, {"perspective", {{"znear", 0.01}, {"yfov", 1}}}}});
        camera["nodes"][1]["camera"] = 0;
        require(hierarchy(camera).nodes[1].camera == 0, "Infinite perspective rejected");
        camera["cameras"][0]["perspective"]["zfar"] = 0.001;
        rejects([&] { hierarchy(camera); }, "perspective camera");
        camera["cameras"][0] = {
            {"type", "orthographic"},
            {"orthographic", {{"znear", 0}, {"zfar", 10}, {"xmag", -2}, {"ymag", 3}}}};
        require(hierarchy(camera).nodes[1].camera == 0,
                "Valid reflected orthographic projection rejected");
        camera["cameras"][0]["orthographic"]["ymag"] = 0;
        rejects([&] { hierarchy(camera); }, "orthographic camera");
        // Deep tree plus many joints: iterative traversal and bounded LCA work.
        Json deep{{"nodes", Json::array()}, {"skins", Json::array()}};
        constexpr std::size_t count = 100000;
        for (std::size_t i = 0; i < count; ++i)
            deep["nodes"].push_back(i + 1 == count ? Json::object() : Json{{"children", {i + 1}}});
        Json joints = Json::array();
        for (std::size_t i = count - 40000; i < count; ++i)
            joints.push_back(i);
        deep["skins"].push_back({{"joints", std::move(joints)}});
        const auto long_chain = hierarchy(deep);
        require(long_chain.parent_first.size() == count &&
                    long_chain.skins[0].common_root == count - 40000,
                "Deep hierarchy common root is incorrect");
        deep["nodes"][count - 1]["children"] = {0};
        rejects([&] { hierarchy(deep); }, "cycle");
        std::cout << "glTF hierarchy, source transforms, scene membership, cameras and skin "
                     "metadata passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
