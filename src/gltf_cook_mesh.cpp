#include "gltf_native.hpp"
#include <algorithm>

namespace forge::asset_detail {
ProcessedMesh cook_gltf_mesh(const NativeGltfDocument& document, std::size_t mesh_index,
                             const MeshProcessingOptions& options,
                             ExcessSkinInfluences skin_policy) {
    const auto& source = document.source().document;
    const auto& mesh = source.at("meshes").at(mesh_index);
    const auto materials = source.contains("materials") ? source.at("materials").size() : 0;
    if (materials >= 65536)
        throw std::runtime_error("Model material slots exceed cooked mesh profile");
    // One mesh may be attached to several skins. Every source joint index must
    // be valid for every binding; the draw palette stays independent of AssetIds.
    std::size_t joint_limit = 65536;
    bool bound_skin = false;
    for (const auto& node : document.hierarchy().nodes)
        if (node.mesh == mesh_index && node.skin != gltf_no_index) {
            bound_skin = true;
            joint_limit =
                std::min(joint_limit, document.hierarchy().skins.at(node.skin).joints.size());
        }
    std::vector<std::string> skin_diagnostics;
    MeshData result;
    // Slot zero is the engine default. Source material i occupies slot i+1.
    result.material_slots = static_cast<std::uint32_t>(materials + 1);
    MeshLod lod;
    std::size_t target_count = 0;
    auto streams = [](const auto& attributes) {
        std::vector<MeshStream> result;
        for (const auto& [name, values] : attributes)
            result.push_back({name, values.components, values.values});
        return result;
    };
    for (std::size_t i = 0; i < mesh.at("primitives").size(); ++i) {
        auto native = document.primitive(mesh_index, i);
        if (native.vertex_count > UINT32_MAX)
            throw std::runtime_error("Model vertex count exceeds cooked mesh index width");
        MeshPart part;
        switch (native.topology) {
        case NativePrimitiveTopology::Points:
            part.topology = MeshTopology::Points;
            break;
        case NativePrimitiveTopology::Lines:
            part.topology = MeshTopology::Lines;
            break;
        case NativePrimitiveTopology::Triangles:
            part.topology = MeshTopology::Triangles;
            break;
        }
        if (bound_skin && !native.integer_attributes.contains("JOINTS_0"))
            throw std::runtime_error("Skinned mesh primitive has no joint/weight data");
        if (bound_skin) {
            const auto skin = prepare_gltf_skin_influences(native, joint_limit, skin_policy);
            part.joint_palette = skin.palette;
            std::erase_if(native.integer_attributes,
                          [](const auto& x) { return x.first.starts_with("JOINTS_"); });
            std::erase_if(native.attributes,
                          [](const auto& x) { return x.first.starts_with("WEIGHTS_"); });
            NativeGltfValues<std::uint32_t> joints{native.vertex_count, 4, {}};
            NativeGltfValues<float> weights{native.vertex_count, 4, {}};
            joints.values.reserve(native.vertex_count * 4);
            weights.values.reserve(native.vertex_count * 4);
            for (const auto& vertex : skin.vertices) {
                joints.values.insert(joints.values.end(), vertex.joints.begin(),
                                     vertex.joints.end());
                weights.values.insert(weights.values.end(), vertex.weights.begin(),
                                      vertex.weights.end());
            }
            native.integer_attributes.emplace("JOINTS_0", std::move(joints));
            native.attributes.emplace("WEIGHTS_0", std::move(weights));
            if (skin.reduced_vertices)
                skin_diagnostics.push_back(
                    std::to_string(skin.reduced_vertices) +
                    " vertices reduced to four skin influences by explicit policy");
            if (skin.renormalized_vertices)
                skin_diagnostics.push_back(std::to_string(skin.renormalized_vertices) +
                                           " vertices had skin weights renormalized");
        }
        part.vertices = static_cast<std::uint32_t>(native.vertex_count);
        part.material_slot = static_cast<std::uint32_t>(native.material + 1);
        part.streams = streams(native.attributes);
        for (const auto& [name, values] : native.integer_attributes)
            part.streams.push_back({name, values.components, values.values});
        part.indices = native.indices;
        for (const auto& target : native.morph_targets)
            part.morph_targets.push_back(streams(target));
        target_count = part.morph_targets.size();
        part.bounds = {native.minimum, native.maximum};
        lod.parts.push_back(std::move(part));
    }
    for (std::size_t i = 0; i < target_count; ++i) {
        result.morph_names.push_back("Target " + std::to_string(i + 1));
        result.morph_defaults.push_back(
            mesh.contains("weights") ? mesh.at("weights").at(i).get<float>() : 0.f);
    }
    // extras are optional labels, never identity or runtime topology authority.
    if (mesh.contains("extras") && mesh.at("extras").is_object()) {
        const auto& extras = mesh.at("extras");
        if (extras.contains("targetNames") && extras.at("targetNames").is_array() &&
            extras.at("targetNames").size() == target_count)
            for (std::size_t i = 0; i < target_count; ++i) {
                const auto& label = extras.at("targetNames").at(i);
                if (label.is_string() && label.get_ref<const std::string&>().size() <= 256 &&
                    label.get_ref<const std::string&>().find('\0') == std::string::npos)
                    result.morph_names[i] = label.get<std::string>();
            }
    }
    result.lods.push_back(std::move(lod));
    validate_mesh(result);
    auto processed = process_mesh(result, options);
    processed.diagnostics.insert(processed.diagnostics.end(), skin_diagnostics.begin(),
                                 skin_diagnostics.end());
    return processed;
}
MeshData cook_gltf_mesh(const NativeGltfDocument& document, std::size_t mesh_index) {
    return cook_gltf_mesh(document, mesh_index, {}).mesh;
}
} // namespace forge::asset_detail
