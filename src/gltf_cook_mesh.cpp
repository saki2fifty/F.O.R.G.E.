#include "gltf_native.hpp"

namespace forge::asset_detail {
ProcessedMesh cook_gltf_mesh(const NativeGltfDocument& document, std::size_t mesh_index,
                             const MeshProcessingOptions& options) {
    const auto& source = document.source().document;
    const auto& mesh = source.at("meshes").at(mesh_index);
    const auto materials = source.contains("materials") ? source.at("materials").size() : 0;
    if (materials >= 65536)
        throw std::runtime_error("Model material slots exceed cooked mesh profile");
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
        const auto native = document.primitive(mesh_index, i);
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
    return process_mesh(result, options);
}
MeshData cook_gltf_mesh(const NativeGltfDocument& document, std::size_t mesh_index) {
    return cook_gltf_mesh(document, mesh_index, {}).mesh;
}
} // namespace forge::asset_detail
