#pragma once
#include "gltf_native.hpp"
#include "gltf_scene.hpp"
#include "gltf_validation.hpp"
#include <cmath>
#include <set>

namespace forge::asset_detail {
struct GltfMeshLodGroup {
    std::size_t node{};
    std::vector<std::size_t> meshes;
    std::vector<float> coverage;
    std::vector<std::size_t> alternatives;
    bool omitted_cull_hint = false;
};
// MSFT_lod replaces nodes, not mesh-array entries. This adapter admits the
// geometry-only subset that one existing renderable can represent faithfully.
// Unsupported node/subtree/material switching is rejected before publication.
inline std::vector<GltfMeshLodGroup> gltf_mesh_lods(const NativeGltfDocument& native,
                                                    const GltfSceneMetadata& metadata) {
    using Json = nlohmann::json;
    const auto& doc = native.scene_source().document;
    const auto& hierarchy = native.hierarchy();
    const auto nodes = doc.value("nodes", Json::array());
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(std::string("MSFT_lod: ") + why);
    };
    auto extension = [](const Json& row) -> const Json* {
        if (const auto e = row.find("extensions"); e != row.end()) {
            if (!e->is_object())
                throw std::runtime_error("MSFT_lod: malformed extension object");
            if (const auto at = e->find("MSFT_lod"); at != e->end())
                return &*at;
        }
        return nullptr;
    };
    for (const auto& material : doc.value("materials", Json::array()))
        require(!extension(material), "material-level replacement is not in the mesh LOD profile");
    std::set<std::size_t> animated;
    for (const auto& animation : doc.value("animations", Json::array()))
        for (const auto& channel : animation.at("channels")) {
            const auto& target = channel.at("target");
            if (target.contains("node"))
                animated.insert(gltf_detail::size_value(target.at("node")));
        }
    std::vector<GltfMeshLodGroup> result;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto* ext = extension(nodes[i]);
        if (!ext)
            continue;
        require(ext->is_object() && ext->contains("ids"), "node LOD requires an ids array");
        const auto& ids = ext->at("ids");
        require(ids.is_array() && !ids.empty() && ids.size() < MeshLimits{}.lods,
                "node LOD count exceeds the 16-level mesh profile");
        const auto& base = hierarchy.nodes.at(i);
        require(base.mesh != gltf_no_index, "highest LOD must have a mesh");
        require(!animated.contains(i), "independently animated LOD owner nodes are unsupported");
        require(!nodes[i].contains("children") || nodes[i].at("children").empty(),
                "subtree replacement needs a node LOD adapter; mesh LOD nodes must be leaves");
        GltfMeshLodGroup group{i, {base.mesh}, {1}};
        std::set<std::size_t> unique{i};
        for (const auto& id : ids) {
            const auto n = gltf_detail::size_value(id);
            require(n < nodes.size() && unique.insert(n).second,
                    "lower node index is missing, repeated or self-referential");
            const auto& lower = hierarchy.nodes[n];
            require(!extension(nodes[n]), "nested node LOD replacement is unsupported");
            require(lower.mesh != gltf_no_index && lower.parent == gltf_no_index &&
                        (!nodes[n].contains("children") || nodes[n].at("children").empty()),
                    "lower LOD must be a root mesh leaf without another structural parent");
            require(lower.matrix == base.matrix && lower.skin == base.skin &&
                        lower.morph_weights == base.morph_weights,
                    "mesh LODs must share the local transform, skin and node morph weights");
            require(!animated.contains(n),
                    "independently animated lower LOD nodes are unsupported");
            require(lower.camera == gltf_no_index && metadata.nodes.at(n).at("light").is_null(),
                    "lower LOD camera/light replacement cannot be represented by a mesh");
            for (const auto* flag : {"visible", "selectable"})
                require(metadata.nodes.at(n).at(flag) == metadata.nodes.at(i).at(flag),
                        "lower LOD visibility/selectability differs from its owner");
            group.meshes.push_back(lower.mesh);
            group.alternatives.push_back(n);
            group.coverage.push_back(std::ldexp(1.f, -int(group.coverage.size())));
        }
        const auto extras = nodes[i].value("extras", Json{});
        if (extras.is_object() && extras.contains("MSFT_screencoverage")) {
            const auto& hints = extras.at("MSFT_screencoverage");
            require(hints.is_array() && hints.size() == group.meshes.size(),
                    "screen-coverage hints must contain one value per LOD");
            double previous = 1;
            for (std::size_t l = 0; l < hints.size(); ++l) {
                require(hints[l].is_number(), "screen-coverage hint is not numeric");
                const double value = hints[l].get<double>();
                require(std::isfinite(value) && value >= 0 && value < previous,
                        "screen-coverage hints must decrease within [0,1)");
                if (l + 1 < group.coverage.size()) {
                    const auto converted = float(value);
                    require(converted > 0 && converted < group.coverage[l],
                            "screen-coverage transition is not representable");
                    group.coverage[l + 1] = converted;
                } else
                    group.omitted_cull_hint = value > 0;
                previous = value;
            }
        }
        result.push_back(std::move(group));
    }
    return result;
}
} // namespace forge::asset_detail
