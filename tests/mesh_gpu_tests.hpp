#pragma once
#include "mesh_gpu.hpp"
void check_mesh_upload(forge::DiligentPresentation& presentation,
                       Diligent::IDeviceContext* context) {
    using namespace Diligent;
    auto* device = presentation.device();
    forge::MeshPart part;
    part.vertices = 3;
    part.indices = {2, 0, 1};
    part.streams = {
        {"POSITION", 3, std::vector<float>{-.5f, -.5f, 0, .5f, -.5f, 0, 0, .5f, 0}},
        {"NORMAL", 3, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1}},
        {"TANGENT", 4, std::vector<float>{1, 0, 0, -1, 1, 0, 0, 1, 1, 0, 0, -1}},
        {"TEXCOORD_0", 2, std::vector<float>{0, 0, 1, 0, 0, 1}},
        {"JOINTS_0", 4, std::vector<std::uint32_t>{0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0}},
        {"WEIGHTS_0", 4, std::vector<float>{.75f, .25f, 0, 0, .5f, .5f, 0, 0, 0, 1, 0, 0}}};
    part.joint_palette = {5, 2};
    part.bounds = forge::mesh_bounds(part);
    part.morph_targets = {{{"POSITION", 3, std::vector<float>{0, 0, .25f, 0, 0, .5f, 0, 0, .75f}},
                           {"TEXCOORD_0", 2, std::vector<float>{.1f, 0, .2f, 0, .3f, 0}}}};
    forge::MeshData mesh;
    mesh.morph_names = {"Raised"};
    mesh.morph_defaults = {-1};
    mesh.lods = {{1, {part}}, {.25f, {part}}};
    auto gpu = forge::upload_mesh(device, mesh);
    require(gpu.lods.size() == 2 && gpu.lods[1].screen_coverage == .25f,
            "GPU upload discarded LODs");
    auto bytes = [&](IBuffer* resource) {
        BufferDesc desc;
        desc.Name = "FORGE mesh readback";
        desc.Size = resource->GetDesc().Size;
        desc.Usage = USAGE_STAGING;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<IBuffer> staging;
        device->CreateBuffer(desc, nullptr, &staging);
        require(bool(staging), "Mesh staging allocation failed");
        context->CopyBuffer(resource, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0,
                            desc.Size, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->WaitForIdle();
        void* mapped = nullptr;
        context->MapBuffer(staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT, mapped);
        require(mapped != nullptr, "Mesh staging map failed");
        std::vector<std::byte> result(std::size_t(desc.Size));
        std::memcpy(result.data(), mapped, result.size());
        context->UnmapBuffer(staging, MAP_READ);
        context->FinishFrame();
        return result;
    };
    std::size_t allocated = 0;
    for (const auto& lod : gpu.lods) {
        const auto& uploaded = lod.parts.at(0);
        require(uploaded.joint_palette == part.joint_palette && uploaded.vertex_count == 3 &&
                    uploaded.index_count == 3 && uploaded.material_slot == 0 &&
                    uploaded.morph_defaults == mesh.morph_defaults &&
                    uploaded.bounds.minimum[2] <= -.75f && uploaded.bounds.minimum[2] > -.751f &&
                    uploaded.bounds.maximum[2] >= -.25f && uploaded.bounds.maximum[2] < -.249f,
                "GPU upload lost mesh draw/binding metadata");
        auto vertices = bytes(uploaded.vertices);
        for (const auto& stream : part.streams) {
            const auto* attribute = uploaded.find(stream.semantic);
            require(attribute && attribute->components == stream.components,
                    "GPU mesh lost a vertex channel");
            std::visit(
                [&](const auto& data) {
                    using Scalar = typename std::decay_t<decltype(data)>::value_type;
                    require(attribute->type ==
                                (std::is_same_v<Scalar, float> ? VT_FLOAT32 : VT_UINT32),
                            "Integer joint data was reinterpreted as float");
                    for (unsigned v = 0; v < part.vertices; ++v)
                        require(
                            std::memcmp(vertices.data() + v * uploaded.stride + attribute->offset,
                                        data.data() + v * stream.components,
                                        stream.components * sizeof(Scalar)) == 0,
                            "GPU vertex interleaving changed cooked values");
                },
                stream.values);
        }
        auto indices = bytes(uploaded.indices);
        require(uploaded.index_type == VT_UINT16 && indices.size() == part.indices.size() * 2,
                "Small GPU mesh did not use compact indices");
        for (std::size_t i = 0; i < part.indices.size(); ++i) {
            Uint16 value;
            std::memcpy(&value, indices.data() + i * 2, 2);
            require(value == part.indices[i], "GPU upload reordered indices/winding");
        }
        auto morphs = bytes(uploaded.morphs);
        for (const auto& stream : part.morph_targets[0]) {
            const auto* attribute = uploaded.morph_targets.at(0).find(stream.semantic);
            require(attribute != nullptr, "GPU upload lost a morph channel");
            const auto& data = std::get<std::vector<float>>(stream.values);
            require(std::memcmp(morphs.data() + attribute->offset, data.data(),
                                data.size() * sizeof(float)) == 0,
                    "GPU upload changed or misidentified morph delta streams");
        }
        allocated += vertices.size() + indices.size() + morphs.size();
    }
    require(gpu.buffer_bytes == allocated, "GPU allocation accounting differs from native buffers");
    mesh.lods[0].parts[0].indices[0] = 100;
    bool rejected = false;
    try {
        forge::upload_mesh(device, mesh);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && bytes(gpu.lods[0].parts[0].indices).size() == 6,
            "Invalid mesh replacement damaged the previous GPU resource");
    for (const unsigned last : {65535u, 65536u}) {
        forge::MeshData large;
        forge::MeshPart p;
        p.vertices = 65537;
        p.topology = forge::MeshTopology::Points;
        p.streams = {{"POSITION", 3, std::vector<float>(std::size_t(p.vertices) * 3)}};
        p.indices = {last};
        p.bounds = forge::mesh_bounds(p);
        large.lods = {{1, {p}}};
        auto uploaded = forge::upload_mesh(device, large);
        const auto& item = uploaded.lods[0].parts[0];
        const auto data = bytes(item.indices);
        std::uint32_t value = 0;
        for (unsigned i = 0; i < data.size(); ++i)
            value |= std::to_integer<std::uint32_t>(data[i]) << (8 * i);
        require(item.index_type == (last == 65535 ? VT_UINT16 : VT_UINT32) &&
                    data.size() == (last == 65535 ? 2u : 4u) && value == last &&
                    uploaded.buffer_bytes == p.vertices * 12 + data.size(),
                "Native index-width boundary lost exact values/accounting");
    }
}
