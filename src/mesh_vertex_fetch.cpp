#include "mesh_vertex_fetch.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
namespace forge {
MeshVertexFetch mesh_vertex_fetch(const GpuMeshPart& mesh, const PbrMaterialProfile& material) {
    using namespace Diligent;
    const auto require = [](bool ok, const std::string& why) {
        if (!ok)
            throw std::runtime_error("Mesh vertex fetch: " + why);
    };
    require(mesh.vertices && mesh.vertex_count && mesh.stride && mesh.stride % 4 == 0,
            "missing or invalid vertex buffer");
    const auto& desc = mesh.vertices->GetDesc();
    require((desc.BindFlags & BIND_SHADER_RESOURCE) && desc.Mode == BUFFER_MODE_RAW &&
                std::uint64_t(mesh.vertex_count) * mesh.stride <= desc.Size &&
                desc.Size <= std::numeric_limits<std::uint32_t>::max(),
            "raw vertex buffer range is unavailable");
    MeshVertexFetch result;
    std::set<unsigned> uv_sets;
    for (const auto& [name, texture] : material.values.textures) {
        (void)name;
        uv_sets.insert(texture.uv_set);
    }
    // Built-in PBR has a finite set of roles. Unknown/custom shader declarations
    // go through their independent shader layout rather than this model adapter.
    require(uv_sets.size() <= 19, "built-in UV role count exceeds the material profile");
    result.uv_sets.assign(uv_sets.begin(), uv_sets.end());
    result.source =
        "ByteAddressBuffer g_MeshVertices;\nstruct ForgeMeshVertex {\n"
        "float3 Position, Normal; float4 Tangent, Color; uint4 Joints; float4 Weights;\n"
        "float2 UV[" +
        std::to_string(std::max<std::size_t>(1, uv_sets.size())) +
        "]; };\nForgeMeshVertex ForgeLoadMeshVertex(uint id) {\n"
        "ForgeMeshVertex v = (ForgeMeshVertex)0; v.Color=1; v.Weights=float4(1,0,0,0);\n"
        "uint address = id * " +
        std::to_string(mesh.stride) + "u;\n";
    auto load = [&](const std::string& semantic, const std::string& destination, unsigned low,
                    unsigned high, VALUE_TYPE type, bool required) {
        const auto* attribute = mesh.find(semantic);
        require(attribute || !required, "required channel missing: " + semantic);
        if (!attribute)
            return false;
        require(attribute->type == type && attribute->components >= low &&
                    attribute->components <= high && attribute->offset % 4 == 0 &&
                    attribute->offset <= mesh.stride &&
                    attribute->components * 4 <= mesh.stride - attribute->offset,
                "invalid type, width or byte range: " + semantic);
        const auto count = attribute->components;
        std::string expression = "g_MeshVertices.Load" + (count == 1 ? "" : std::to_string(count)) +
                                 "(address + " + std::to_string(attribute->offset) + "u)";
        if (type == VT_FLOAT32)
            expression = "asfloat(" + expression + ")";
        if (semantic == "COLOR_0" && count == 3)
            expression = "float4(" + expression + ",1)";
        result.source += "v." + destination + " = " + expression + ";\n";
        return true;
    };
    load("POSITION", "Position", 3, 3, VT_FLOAT32, true);
    result.normal = load("NORMAL", "Normal", 3, 3, VT_FLOAT32, false);
    result.tangent = load("TANGENT", "Tangent", 4, 4, VT_FLOAT32, false);
    result.color = load("COLOR_0", "Color", 3, 4, VT_FLOAT32, false);
    result.skin = mesh.find("JOINTS_0") || mesh.find("WEIGHTS_0");
    if (result.skin) {
        require(!mesh.joint_palette.empty(), "skin channels need an admitted draw palette");
        load("JOINTS_0", "Joints", 4, 4, VT_UINT32, true);
        load("WEIGHTS_0", "Weights", 4, 4, VT_FLOAT32, true);
    }
    for (std::size_t i = 0; i < result.uv_sets.size(); ++i)
        load("TEXCOORD_" + std::to_string(result.uv_sets[i]), "UV[" + std::to_string(i) + "]", 2, 2,
             VT_FLOAT32, true);
    result.source += "return v; }\n";
    return result;
}
} // namespace forge
