#include "mesh_vertex_fetch.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
namespace forge {
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error("Mesh vertex fetch: " + why);
}
// Called while ForgeLoadMeshVertex is open, after base attributes and before
// its return. Pure string/table preparation; device checks stay in the caller.
void append_morph_fetch(MeshVertexFetch& result, const GpuMeshPart& mesh,
                        std::uint64_t delta_bytes) {
    if (!mesh.morph_targets.empty()) {
        require(mesh.morph_targets.size() <= 256 && delta_bytes <= UINT32_MAX,
                "morph draw exceeds buffer/target profile");
        result.morph_count = static_cast<unsigned>(mesh.morph_targets.size());
        std::vector<std::string> channels{"POSITION", "NORMAL", "TANGENT", "COLOR_0"};
        for (auto uv : result.uv_sets)
            channels.push_back("TEXCOORD_" + std::to_string(uv));
        const unsigned rows = static_cast<unsigned>((channels.size() + 3) / 4);
        result.morph_offsets.resize(rows * result.morph_count);
        for (auto& row : result.morph_offsets)
            row.fill(UINT32_MAX);
        for (unsigned t = 0; t < result.morph_count; ++t)
            for (unsigned c = 0; c < channels.size(); ++c) {
                const auto* attribute = mesh.morph_targets[t].find(channels[c]);
                if (!attribute)
                    continue;
                const unsigned width = c == 3 ? attribute->components : c >= 4 ? 2 : 3;
                require(attribute->type == Diligent::VT_FLOAT32 && attribute->components == width &&
                            width >= 2 && width <= 4 && (c != 3 || width >= 3) &&
                            attribute->offset % 4 == 0 && attribute->offset <= delta_bytes &&
                            std::uint64_t(mesh.vertex_count) * width * 4 <=
                                delta_bytes - attribute->offset,
                        "morph channel type/width/range is invalid: " + channels[c]);
                result.morph_offsets[t * rows + c / 4][c % 4] = attribute->offset | (width - 1);
            }
        const auto prefix = "ByteAddressBuffer g_ForgeMorphDeltas;\n"
                            "cbuffer ForgeMorphOffsets {uint4 g_MorphOffsets[" +
                            std::to_string(result.morph_offsets.size()) +
                            "];};\n"
                            "cbuffer ForgeMorphWeights {float4 g_MorphWeights[" +
                            std::to_string((result.morph_count + 3) / 4) +
                            "];};\n"
                            "float4 ForgeMorphDelta(uint target,uint channel,uint vertex) {\n"
                            "uint packed=g_MorphOffsets[target*" +
                            std::to_string(rows) +
                            "+channel/4][channel%4];float4 delta=0;\n"
                            "if(packed!=0xffffffffu) {\n"
                            "uint width=(packed&3)+1;uint at=(packed&~3u)+vertex*width*4;\n"
                            "if(width==2)delta.xy=asfloat(g_ForgeMorphDeltas.Load2(at));\n"
                            "else if(width==3)delta.xyz=asfloat(g_ForgeMorphDeltas.Load3(at));\n"
                            "else delta=asfloat(g_ForgeMorphDeltas.Load4(at));}\n"
                            "return delta;}\n";
        result.source = prefix + result.source;
        result.source += "[loop]for(uint t=0;t<" + std::to_string(result.morph_count) +
                         ";t++) {float w=g_MorphWeights[t/4][t%4];if(w!=0) {\n"
                         "v.Position+=ForgeMorphDelta(t,0,id).xyz*w;\n";
        if (result.normal)
            result.source += "v.Normal+=ForgeMorphDelta(t,1,id).xyz*w;\n";
        if (result.tangent)
            result.source += "v.Tangent.xyz+=ForgeMorphDelta(t,2,id).xyz*w;\n";
        if (result.color)
            result.source += "v.Color+=ForgeMorphDelta(t,3,id)*w;\n";
        for (unsigned u = 0; u < result.uv_sets.size(); ++u)
            result.source += "v.UV[" + std::to_string(u) + "]+=ForgeMorphDelta(t," +
                             std::to_string(u + 4) + ",id).xy*w;\n";
        result.source += "}}v.Color=saturate(v.Color);\n";
    }
}
} // namespace
MeshVertexFetch mesh_vertex_fetch(const GpuMeshPart& mesh, const PbrMaterialProfile& material,
                                  bool enable_skin) {
    using namespace Diligent;
    require(mesh.vertices && mesh.vertex_count && mesh.stride && mesh.stride % 4 == 0,
            "missing or invalid vertex buffer");
    const auto& desc = mesh.vertices->GetDesc();
    require((desc.BindFlags & BIND_SHADER_RESOURCE) && desc.Mode == BUFFER_MODE_RAW &&
                std::uint64_t(mesh.vertex_count) * mesh.stride <= desc.Size &&
                desc.Size <= std::numeric_limits<std::uint32_t>::max(),
            "raw vertex buffer range is unavailable");
    MeshVertexFetch result;
    result.triangles = mesh.topology == PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    result.skin = enable_skin && (mesh.find("JOINTS_0") || mesh.find("WEIGHTS_0"));
    if (result.skin) {
        require(!mesh.joint_palette.empty(), "skin channels need an admitted draw palette");
        load("JOINTS_0", "Joints", 4, 4, VT_UINT32, true);
        load("WEIGHTS_0", "Weights", 4, 4, VT_FLOAT32, true);
    }
    for (std::size_t i = 0; i < result.uv_sets.size(); ++i)
        load("TEXCOORD_" + std::to_string(result.uv_sets[i]), "UV[" + std::to_string(i) + "]", 2, 2,
             VT_FLOAT32, true);
    if (!mesh.morph_targets.empty()) {
        require(bool(mesh.morphs), "morph draw requires a delta buffer");
        const auto& delta = mesh.morphs->GetDesc();
        require(delta.Mode == BUFFER_MODE_RAW && (delta.BindFlags & BIND_SHADER_RESOURCE),
                "morph delta raw binding is invalid");
        append_morph_fetch(result, mesh, delta.Size);
    }
    result.source += "return v; }\n";
    return result;
}
} // namespace forge
