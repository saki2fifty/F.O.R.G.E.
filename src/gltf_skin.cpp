#include "gltf_native.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace forge::asset_detail {
std::vector<std::array<float, 16>>
NativeGltfDocument::inverse_bind_matrices(std::size_t skin) const {
    if (skin >= hierarchy_.skins.size())
        throw std::runtime_error("glTF inverse bind skin index is invalid");
    const auto& binding = hierarchy_.skins[skin];
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<std::array<float, 16>> result(binding.joints.size(), identity);
    if (binding.inverse_bind_accessor == gltf_no_index)
        return result;
    const auto matrices = floats(binding.inverse_bind_accessor);
    if (matrices.components != 16 || matrices.count < result.size())
        throw std::runtime_error("glTF inverse bind dimensions changed after admission");
    for (std::size_t i = 0; i < matrices.count; ++i) {
        const auto* m = matrices.values.data() + i * 16;
        if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1)
            throw std::runtime_error("glTF inverse bind matrix must be affine");
        // Extra matrices are allowed by glTF but do not add joints. Validate all
        // supplied values, then preserve the actual ordered skin binding only.
        if (i < result.size())
            std::copy_n(m, 16, result[i].begin());
    }
    return result;
}
NativeSkinInfluences prepare_gltf_skin_influences(const NativeMeshPrimitive& primitive,
                                                  std::size_t joint_count,
                                                  ExcessSkinInfluences policy) {
    if (!joint_count || joint_count > 65536 || !primitive.vertex_count ||
        primitive.vertex_count > (512 * 1024 * 1024) / sizeof(NativeSkinVertex))
        throw std::runtime_error("glTF skin working data exceeds profile limits");
    if (policy != ExcessSkinInfluences::Reject && policy != ExcessSkinInfluences::ReduceToFour)
        throw std::runtime_error("Invalid excess skin influence policy");
    struct Set {
        const NativeGltfValues<std::uint32_t>* joints;
        const NativeGltfValues<float>* weights;
    };
    std::vector<Set> sets;
    if (!primitive.integer_attributes.contains("JOINTS_0") ||
        primitive.integer_attributes.size() > 32)
        throw std::runtime_error("glTF skinned primitive requires bounded joint/weight streams");
    for (const auto& [name, joints] : primitive.integer_attributes) {
        if (!name.starts_with("JOINTS_"))
            continue;
        const auto weight = primitive.attributes.find("WEIGHTS_" + name.substr(7));
        if (weight == primitive.attributes.end() || joints.components != 4 ||
            weight->second.components != 4 || joints.count != primitive.vertex_count ||
            weight->second.count != primitive.vertex_count ||
            joints.values.size() != primitive.vertex_count * 4 ||
            weight->second.values.size() != primitive.vertex_count * 4)
            throw std::runtime_error("glTF skin stream shape/count mismatch");
        sets.push_back({&joints, &weight->second});
    }
    if (sets.empty() || primitive.vertex_count > (128 * 1024 * 1024) / (sets.size() * 4))
        throw std::runtime_error("glTF skin aggregate influence budget exceeded");
    NativeSkinInfluences result;
    result.vertices.resize(primitive.vertex_count);
    std::set<std::uint32_t> palette;
    struct Influence {
        std::uint32_t joint;
        double weight;
    };
    std::vector<Influence> active;
    active.reserve(sets.size() * 4);
    for (std::size_t vertex = 0; vertex < primitive.vertex_count; ++vertex) {
        active.clear();
        double sum = 0;
        for (const auto& set : sets)
            for (unsigned k = 0; k < 4; ++k) {
                const auto at = vertex * 4 + k;
                const auto joint = set.joints->values[at];
                const auto weight = set.weights->values[at];
                if (joint >= joint_count)
                    throw std::runtime_error(
                        "glTF skin joint index exceeds skin.joints, including zero-weight slots");
                if (!std::isfinite(weight) || weight < 0 || weight > 1)
                    throw std::runtime_error("glTF skin weight is outside finite [0,1]");
                if (weight > 0) {
                    active.push_back({joint, weight});
                    sum += weight;
                }
            }
        std::sort(active.begin(), active.end(),
                  [](const auto& a, const auto& b) { return a.joint < b.joint; });
        for (std::size_t i = 1; i < active.size(); ++i)
            if (active[i - 1].joint == active[i].joint)
                throw std::runtime_error("glTF skin has duplicate nonzero influence for a joint");
        if (active.empty() || !std::isfinite(sum) || sum <= 0)
            throw std::runtime_error("glTF skin weights cannot be renormalized from zero");
        if (std::abs(sum - 1) > 2e-7 * active.size())
            ++result.renormalized_vertices;
        if (active.size() > 4) {
            if (policy == ExcessSkinInfluences::Reject)
                throw std::runtime_error(
                    "glTF vertex exceeds four influences; select explicit reduction policy");
            ++result.reduced_vertices;
        }
        // Deterministic tie-breaking is independent of attribute-set order.
        std::sort(active.begin(), active.end(), [](const auto& a, const auto& b) {
            return a.weight == b.weight ? a.joint < b.joint : a.weight > b.weight;
        });
        if (active.size() > 4)
            active.resize(4);
        sum = 0;
        for (const auto& value : active)
            sum += value.weight;
        auto& output = result.vertices[vertex];
        for (std::size_t i = 0; i < active.size(); ++i) {
            output.joints[i] = static_cast<std::uint16_t>(active[i].joint);
            output.weights[i] = static_cast<float>(active[i].weight / sum);
            palette.insert(active[i].joint);
        }
        if (palette.size() > 256)
            throw std::runtime_error(
                "glTF draw requires more than 256 palette joints; partitioning is required");
    }
    result.palette.assign(palette.begin(), palette.end());
    for (auto& vertex : result.vertices)
        for (unsigned i = 0; i < 4; ++i)
            if (vertex.weights[i] > 0)
                vertex.joints[i] = static_cast<std::uint16_t>(
                    std::lower_bound(result.palette.begin(), result.palette.end(),
                                     vertex.joints[i]) -
                    result.palette.begin());
            else
                vertex.joints[i] = 0;
    return result;
}
} // namespace forge::asset_detail
