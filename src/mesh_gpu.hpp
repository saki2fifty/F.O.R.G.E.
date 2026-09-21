#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <forge/mesh_asset.hpp>
namespace forge {
struct GpuMeshAttribute {
    std::string semantic;
    unsigned components{}, offset{};
    Diligent::VALUE_TYPE type = Diligent::VT_UNDEFINED;
};
struct GpuMorphTarget {
    // Byte offsets into the raw immutable delta buffer. Preserve custom channels;
    // the shader adapter selects supported semantics without reinterpreting them.
    std::vector<GpuMeshAttribute> attributes;
    const GpuMeshAttribute* find(std::string_view) const;
};
struct GpuMeshPart {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertices, indices, morphs;
    std::vector<GpuMeshAttribute> attributes;
    std::vector<GpuMorphTarget> morph_targets;
    std::vector<float> morph_defaults;
    std::vector<std::uint32_t> joint_palette;
    Diligent::PRIMITIVE_TOPOLOGY topology = Diligent::PRIMITIVE_TOPOLOGY_UNDEFINED;
    unsigned stride{}, vertex_count{}, index_count{}, material_slot{};
    MeshBounds bounds;
    const GpuMeshAttribute* find(std::string_view) const;
};
struct GpuMeshLod {
    float screen_coverage{};
    std::vector<GpuMeshPart> parts;
};
struct GpuMesh {
    std::vector<GpuMeshLod> lods;
    // Exact allocated buffer bytes, excluding driver-private object overhead.
    std::size_t buffer_bytes{};
};
// One detached candidate. Failure never changes a previously selected resource.
// Uploads retain cooked ordering, including integer joints and tangent handedness.
GpuMesh upload_mesh(Diligent::IRenderDevice*, const MeshData&);
} // namespace forge
