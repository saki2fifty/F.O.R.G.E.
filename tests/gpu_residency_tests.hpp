#pragma once
#include "gpu_residency.hpp"
namespace forge {
struct GpuFailureAsset {
    static constexpr const char* type = "fixture.gpu-failure";
};
struct GpuFailureData {
    bool fail = false;
};
template <> struct ResourceTraits<GpuFailureAsset> {
    using Data = GpuFailureData;
};
namespace gpu_detail {
template <> struct Traits<GpuFailureAsset> {
    using Data = Diligent::RefCntAutoPtr<Diligent::IBuffer>;
    static std::uint64_t bytes(const GpuFailureData&) { return 64; }
    static Data upload(Diligent::IRenderDevice* device, Diligent::IDeviceContext*,
                       const GpuFailureData& value) {
        Diligent::BufferDesc desc;
        desc.Name = "FORGE partial GPU candidate";
        desc.Size = 64;
        desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
        desc.Usage = Diligent::USAGE_IMMUTABLE;
        const std::array<float, 16> values{};
        Diligent::BufferData input{values.data(), 64};
        Data result;
        device->CreateBuffer(desc, &input, &result);
        if (!result)
            throw std::runtime_error("Fixture buffer allocation failed");
        if (value.fail)
            throw std::runtime_error("Fixture candidate fails after native allocation");
        return result;
    }
};
} // namespace gpu_detail
} // namespace forge
void check_gpu_residency(forge::DiligentPresentation& presentation,
                         Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using namespace std::chrono_literals;
    forge::ResourcePool<forge::TextureAsset> cpu;
    const forge::AssetRef<forge::TextureAsset> asset{forge::AssetId::generate()};
    auto load = [](unsigned red) {
        return [red](std::stop_token) {
            auto data = std::make_unique<forge::TextureData>();
            data->width = data->height = 1;
            data->subresources = {{std::byte(red), std::byte{0}, std::byte{0}, std::byte{255}}};
            const auto bytes = data->resident_bytes();
            return forge::ResourceCandidate<forge::TextureAsset>{std::move(data), {bytes}};
        };
    };
    auto ticket = cpu.request(asset, std::string(64, 'a'), 1, load(63));
    require(cpu.wait(ticket, 5s), "GPU fixture CPU texture did not load");
    auto source = cpu.acquire(ticket);
    const auto old_id = source.identity();
    forge::GpuResidency<forge::TextureAsset> gpu(presentation.device(), context, 8);
    auto first = gpu.acquire(source);
    auto shared = gpu.acquire(source);
    require(first.get() == shared.get() && gpu.statistics().payload_bytes == 4,
            "GPU repeated acquisition allocated duplicate texture");
    bool wrong_thread = false;
    std::thread reader([&] {
        try {
            first.get();
        } catch (const std::exception&) {
            wrong_thread = true;
        }
    });
    reader.join();
    require(wrong_thread, "GPU lease permitted off-owner access");
    auto replacement = cpu.request(asset, std::string(64, 'b'), 2, load(127));
    require(cpu.wait(replacement, 5s), "Replacement CPU texture did not load");
    auto next = gpu.acquire(cpu.acquire(replacement));
    require(next.get() != first.get() && gpu.statistics().payload_bytes == 8,
            "GPU replacement overwrote old revision");
    auto third = cpu.request(asset, std::string(64, 'c'), 3, load(255));
    require(cpu.wait(third, 5s), "Third CPU texture did not load");
    bool rejected = false;
    try {
        gpu.acquire(cpu.acquire(third));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && first && next && gpu.statistics().payload_bytes == 8,
            "GPU budget failure damaged old leases");
    auto desc = first.get()->GetDesc();
    desc.Name = "FORGE retiring texture proof";
    desc.BindFlags = BIND_NONE;
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    presentation.device()->CreateTexture(desc, nullptr, &staging);
    require(bool(staging), "GPU retirement staging allocation failed");
    CopyTextureAttribs copy;
    copy.pSrcTexture = first.get();
    copy.pDstTexture = staging;
    copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode =
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    gpu.unload(old_id);
    first = {};
    shared = {};
    gpu.collect();
    require(gpu.statistics().retiring == 1 && gpu.statistics().payload_bytes == 8,
            "Unsubmitted GPU retirement escaped accounting");
    gpu.submit();
    context->WaitForIdle();
    gpu.collect();
    const auto stats = gpu.statistics();
    require(stats.retiring == 0 && stats.payload_bytes == 4 &&
                stats.completed_fence >= stats.submitted_fence,
            "Completed native fence did not retire the old texture");
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    require(mapped.pData && static_cast<const unsigned char*>(mapped.pData)[0] == 63,
            "In-flight old texture copy changed after replacement/retirement");
    context->UnmapTextureSubresource(staging, 0, 0);
    auto final = gpu.acquire(cpu.acquire(third));
    require(final && gpu.statistics().payload_bytes == 8,
            "GPU budget did not recover after fence retirement");
    cpu.close();
    require(bool(next.get()), "GPU realization depended on retired CPU payload");
    gpu.close();
    require(!next && !final, "GPU close did not revoke held leases");
    rejected = false;
    try {
        next.get();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Closed GPU resource remained accessible");
    forge::ResourcePool<forge::GpuFailureAsset> failed_cpu;
    const forge::AssetRef<forge::GpuFailureAsset> failed_asset{forge::AssetId::generate()};
    auto failed_ticket =
        failed_cpu.request(failed_asset, std::string(64, 'd'), 1, [](std::stop_token) {
            return forge::ResourceCandidate<forge::GpuFailureAsset>{
                std::make_unique<forge::GpuFailureData>(true), {1}};
        });
    require(failed_cpu.wait(failed_ticket, 5s), "GPU partial failure fixture did not load");
    forge::GpuResidency<forge::GpuFailureAsset> failures(presentation.device(), context, 64);
    rejected = false;
    try {
        failures.acquire(failed_cpu.acquire(failed_ticket));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && failures.statistics().payload_bytes == 64 &&
                failures.statistics().retiring == 1,
            "Partially failed GPU candidate was dropped from retirement accounting");
    failures.collect();
    require(failures.statistics().payload_bytes == 64, "Failed upload retired without submission");
    failures.submit();
    context->WaitForIdle();
    failures.collect();
    require(failures.statistics().payload_bytes == 0,
            "Failed upload reservation survived completed fence");
    forge::ResourcePool<forge::MeshAsset> mesh_cpu;
    const forge::AssetRef<forge::MeshAsset> mesh_asset{forge::AssetId::generate()};
    auto mesh_ticket = mesh_cpu.request(mesh_asset, std::string(64, 'e'), 1, [](std::stop_token) {
        auto data = std::make_unique<forge::MeshResourceData>();
        forge::MeshPart part;
        part.vertices = 3;
        part.indices = {0, 1, 2};
        part.streams = {{"POSITION", 3, std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0}}};
        part.bounds = forge::mesh_bounds(part);
        data->mesh.lods = {{1, {part}}};
        data->materials = {{0, "default", {}}};
        const auto bytes = data->resident_bytes();
        return forge::ResourceCandidate<forge::MeshAsset>{std::move(data), {bytes}};
    });
    require(mesh_cpu.wait(mesh_ticket, 5s), "GPU fixture mesh did not load");
    forge::GpuResidency<forge::MeshAsset> mesh_gpu(presentation.device(), context, 65536);
    auto mesh_lease = mesh_gpu.acquire(mesh_cpu.acquire(mesh_ticket));
    require(mesh_lease.get().buffer_bytes == 48 && mesh_gpu.statistics().payload_bytes == 48,
            "GPU mesh payload accounting disagrees with uploaded buffers");
    mesh_gpu.submit();
    mesh_cpu.close();
    mesh_gpu.close();
    mesh_gpu.close();
    require(!mesh_lease, "Mesh GPU close did not revoke the native realization");
    failures.close();
    context->FinishFrame();
}
