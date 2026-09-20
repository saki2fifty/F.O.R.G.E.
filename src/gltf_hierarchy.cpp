#include "gltf_native.hpp"
#include "gltf_validation.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace forge::asset_detail {
namespace {
using namespace gltf_detail;
constexpr std::size_t limit = 100000, edge_limit = 1000000;
std::size_t index(const Json& value, std::size_t count, const char* kind) {
    const auto result = size_value(value);
    if (result >= count)
        throw std::runtime_error(std::string("glTF invalid ") + kind + " index");
    return result;
}
double number(const Json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::runtime_error("glTF transform/camera value must be finite");
    return value.get<double>();
}
template <std::size_t N>
std::array<double, N> numbers(const Json& object, const char* key, std::array<double, N> defaults) {
    if (!object.contains(key))
        return defaults;
    const auto& values = object.at(key);
    if (!values.is_array() || values.size() != N)
        throw std::runtime_error(std::string("glTF invalid vector/matrix shape: ") + key);
    for (std::size_t i = 0; i < N; ++i)
        defaults[i] = number(values[i]);
    return defaults;
}
std::array<double, 16> matrix(const Json& node) {
    std::array<double, 16> result{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (node.contains("matrix")) {
        if (node.contains("translation") || node.contains("rotation") || node.contains("scale"))
            throw std::runtime_error("glTF node cannot define both matrix and TRS");
        result = numbers(node, "matrix", result);
        if (result[3] != 0 || result[7] != 0 || result[11] != 0 || result[15] != 1)
            throw std::runtime_error("glTF node matrix must be affine");
        std::array<std::array<double, 3>, 3> columns;
        for (unsigned c = 0; c < 3; ++c) {
            const double length = std::hypot(result[c * 4], result[c * 4 + 1], result[c * 4 + 2]);
            if (!std::isfinite(length) || length == 0)
                throw std::runtime_error("glTF node matrix has a singular column");
            for (unsigned r = 0; r < 3; ++r)
                columns[c][r] = result[c * 4 + r] / length;
        }
        for (unsigned a = 0; a < 3; ++a)
            for (unsigned b = a + 1; b < 3; ++b) {
                double dot = 0;
                for (unsigned r = 0; r < 3; ++r)
                    dot += columns[a][r] * columns[b][r];
                if (std::abs(dot) > 1e-6)
                    throw std::runtime_error("glTF node matrix contains non-TRS shear");
            }
        return result;
    }
    const auto translation = numbers<3>(node, "translation", {0, 0, 0});
    const auto scale = numbers<3>(node, "scale", {1, 1, 1});
    auto q = numbers<4>(node, "rotation", {0, 0, 0, 1});
    const double norm = std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
    if (!std::isfinite(norm) || std::abs(norm - 1) > 0.001)
        throw std::runtime_error("glTF node rotation must be a unit quaternion");
    for (auto& value : q)
        value /= norm;
    const auto x = q[0], y = q[1], z = q[2], w = q[3];
    result = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     0,
              2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     0,
              2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y), 0,
              translation[0],          translation[1],          translation[2],          1};
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned r = 0; r < 3; ++r) {
            result[c * 4 + r] *= scale[c];
            if (!std::isfinite(result[c * 4 + r]))
                throw std::runtime_error("glTF TRS matrix exceeds finite numeric range");
        }
    return result;
}
void cameras(const Json& document) {
    for (const auto& camera : array(document, "cameras", limit)) {
        const auto type = camera.at("type").get<std::string>();
        const auto& p = camera.at(type);
        if (!p.is_object())
            throw std::runtime_error("glTF camera projection must be an object");
        const auto near = number(p.at("znear"));
        if (type == "perspective") {
            const auto fov = number(p.at("yfov"));
            if (camera.contains("orthographic") || near <= 0 || fov <= 0 ||
                fov >= std::numbers::pi ||
                (p.contains("aspectRatio") && number(p.at("aspectRatio")) <= 0) ||
                (p.contains("zfar") && number(p.at("zfar")) <= near))
                throw std::runtime_error("glTF perspective camera parameters are invalid");
        } else if (type == "orthographic") {
            if (camera.contains("perspective") || near < 0 || number(p.at("zfar")) <= near ||
                number(p.at("xmag")) == 0 || number(p.at("ymag")) == 0)
                throw std::runtime_error("glTF orthographic camera parameters are invalid");
        } else
            throw std::runtime_error("glTF camera projection type is invalid");
    }
}
} // namespace
NativeGltfHierarchy validate_gltf_hierarchy(const GltfSourceBundle& source) {
    const auto& document = source.document;
    const auto& nodes = array(document, "nodes", limit);
    const auto& meshes = array(document, "meshes", limit);
    const auto& skins = array(document, "skins", limit);
    const auto& accessors = array(document, "accessors", limit);
    const auto camera_count = array(document, "cameras", limit).size();
    cameras(document);
    NativeGltfHierarchy result;
    result.nodes.resize(nodes.size());
    std::size_t edges = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (!node.is_object())
            throw std::runtime_error("glTF node must be an object");
        auto& output = result.nodes[i];
        output.matrix = matrix(node);
        if (node.contains("mesh"))
            output.mesh = index(node.at("mesh"), meshes.size(), "node mesh");
        if (node.contains("skin")) {
            output.skin = index(node.at("skin"), skins.size(), "node skin");
            if (output.mesh == gltf_no_index)
                throw std::runtime_error("glTF skinned node requires a mesh");
        }
        if (node.contains("camera"))
            output.camera = index(node.at("camera"), camera_count, "node camera");
        if (output.mesh != gltf_no_index) {
            const auto& mesh = meshes[output.mesh];
            const auto& primitives = array(mesh, "primitives", limit);
            if (primitives.empty())
                throw std::runtime_error("glTF mesh requires primitives");
            const auto count = array(primitives[0], "targets", 256).size();
            const auto& weights = node.contains("weights")   ? node.at("weights")
                                  : mesh.contains("weights") ? mesh.at("weights")
                                                             : Json{};
            output.morph_weights.resize(count, 0);
            if (!weights.is_null()) {
                if (!weights.is_array() || weights.size() != count || count == 0)
                    throw std::runtime_error("glTF node morph weight count is invalid");
                for (std::size_t k = 0; k < count; ++k)
                    output.morph_weights[k] = number(weights[k]);
            }
        } else if (node.contains("weights"))
            throw std::runtime_error("glTF morph weights require a mesh");
        const auto& children = array(node, "children", limit);
        if (children.size() > edge_limit - edges)
            throw std::runtime_error("glTF hierarchy edge budget exceeded");
        edges += children.size();
        for (const auto& child : children) {
            auto& parent = result.nodes[index(child, nodes.size(), "child")].parent;
            if (parent != gltf_no_index)
                throw std::runtime_error("glTF child has duplicate or multiple parents");
            parent = i;
        }
    }
    // Iterative traversal plus binary ancestors: deep chains do not recurse, and
    // skin common-root checks do not scan a long chain once for every joint.
    std::vector<std::array<std::size_t, 18>> up(nodes.size());
    std::vector<std::size_t> depth(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (result.nodes[i].parent == gltf_no_index) {
            result.parent_first.push_back(i);
            result.nodes[i].root = i;
            up[i].fill(i);
        }
    for (std::size_t p = 0; p < result.parent_first.size(); ++p) {
        const auto parent = result.parent_first[p];
        for (const auto& child_json : array(nodes[parent], "children", limit)) {
            const auto child = size_value(child_json);
            result.nodes[child].root = result.nodes[parent].root;
            depth[child] = depth[parent] + 1;
            up[child][0] = parent;
            for (unsigned k = 1; k < 18; ++k)
                up[child][k] = up[up[child][k - 1]][k - 1];
            result.parent_first.push_back(child);
        }
    }
    if (result.parent_first.size() != nodes.size())
        throw std::runtime_error("glTF node hierarchy contains a cycle");
    auto common = [&](std::size_t a, std::size_t b) {
        if (result.nodes[a].root != result.nodes[b].root)
            throw std::runtime_error("glTF skin joints do not share a common root");
        if (depth[a] < depth[b])
            std::swap(a, b);
        const auto delta = depth[a] - depth[b];
        for (unsigned k = 0; k < 18; ++k)
            if (delta & (std::size_t{1} << k))
                a = up[a][k];
        if (a == b)
            return a;
        for (int k = 17; k >= 0; --k)
            if (up[a][k] != up[b][k]) {
                a = up[a][k];
                b = up[b][k];
            }
        return up[a][0];
    };
    std::size_t joint_count = 0;
    for (const auto& skin : skins) {
        NativeGltfSkinBinding binding;
        const auto& joints = array(skin, "joints", 65536);
        if (joints.empty() || joints.size() > edge_limit - joint_count)
            throw std::runtime_error("glTF skin joint count exceeds admission limits");
        joint_count += joints.size();
        std::set<std::size_t> unique;
        for (const auto& joint : joints) {
            const auto j = index(joint, nodes.size(), "skin joint");
            if (!unique.insert(j).second)
                throw std::runtime_error("glTF skin has duplicate joints");
            binding.joints.push_back(j);
            binding.common_root =
                binding.common_root == gltf_no_index ? j : common(binding.common_root, j);
        }
        if (skin.contains("skeleton")) {
            binding.skeleton = index(skin.at("skeleton"), nodes.size(), "skeleton");
            if (common(binding.skeleton, binding.common_root) != binding.skeleton)
                throw std::runtime_error("glTF skeleton is not an ancestor of every joint");
        }
        if (skin.contains("inverseBindMatrices")) {
            binding.inverse_bind_accessor =
                index(skin.at("inverseBindMatrices"), accessors.size(), "inverse bind");
            const auto& a = accessors[binding.inverse_bind_accessor];
            if (a.at("type") != "MAT4" || size_value(a.at("componentType")) != 5126 ||
                a.value("normalized", false) || size_value(a.at("count")) < joints.size())
                throw std::runtime_error("glTF inverse bind accessor has invalid shape/count/type");
            if (a.contains("bufferView")) {
                const auto& views = array(document, "bufferViews", limit);
                const auto& view =
                    views[index(a.at("bufferView"), views.size(), "inverse bind view")];
                if (view.contains("byteStride") || view.contains("target"))
                    throw std::runtime_error(
                        "glTF inverse bind matrices cannot use vertex/index view layout");
            }
        }
        result.skins.push_back(std::move(binding));
    }
    // Validate membership by tree roots. A node may occur in several scenes;
    // do not stamp a single scene ID onto it or expand every scene's hierarchy.
    std::map<std::size_t, std::set<std::size_t>> required_roots;
    for (const auto& node : result.nodes)
        if (node.skin != gltf_no_index)
            required_roots[node.root].insert(
                result.nodes[result.skins[node.skin].common_root].root);
    std::size_t scene_roots = 0, membership_checks = 0;
    for (const auto& scene : array(document, "scenes", limit)) {
        if (!scene.is_object())
            throw std::runtime_error("glTF scene must be an object");
        const auto& roots = array(scene, "nodes", limit);
        if (roots.size() > edge_limit - scene_roots)
            throw std::runtime_error("glTF scene root budget exceeded");
        scene_roots += roots.size();
        std::vector<std::size_t> list;
        std::set<std::size_t> unique;
        for (const auto& root_json : roots) {
            const auto root = index(root_json, nodes.size(), "scene root");
            if (result.nodes[root].parent != gltf_no_index || !unique.insert(root).second)
                throw std::runtime_error("glTF scene contains a non-root or duplicate root node");
            list.push_back(root);
        }
        for (auto root : list)
            if (const auto found = required_roots.find(root); found != required_roots.end())
                for (auto required : found->second) {
                    if (++membership_checks > edge_limit)
                        throw std::runtime_error("glTF skin scene membership budget exceeded");
                    if (!unique.contains(required))
                        throw std::runtime_error(
                            "glTF skin common root is missing from mesh scene");
                }
        result.scenes.push_back(std::move(list));
    }
    if (document.contains("scene"))
        result.default_scene = index(document.at("scene"), result.scenes.size(), "default scene");
    return result;
}
} // namespace forge::asset_detail
