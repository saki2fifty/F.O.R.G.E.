#include "presentation_diligent.hpp"
#include "Graphics/Archiver/interface/ArchiverFactoryLoader.h"
#include "render_backend.hpp"
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
namespace forge {
using namespace Diligent;
DiligentPresentation::DiligentPresentation(IRenderDevice* device) : device_(device) {
    if (!device)
        throw std::runtime_error("Presentation requires a render device");
    auto* factory = LoadAndGetArchiverFactory();
    if (!factory)
        throw std::runtime_error("Diligent render-state archiver is unavailable");
    RenderStateCacheCreateInfo info{device, factory};
    info.FileHashMode = RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT;
    info.EnableHotReload = false;
    info.LogLevel = RENDER_STATE_CACHE_LOG_LEVEL_DISABLED;
    CreateRenderStateCache(info, &cache_);
    if (!cache_)
        throw std::runtime_error("Diligent render-state cache creation failed");
    FenceDesc fence;
    fence.Name = "FORGE frame completion";
    device_->CreateFence(fence, &frame_fence_);
    if (!frame_fence_)
        throw std::runtime_error("Presentation frame completion fence unavailable");
}
std::uint64_t DiligentPresentation::pending_frames() const {
    const auto completed = frame_fence_->GetCompletedValue();
    return submitted_frames_ > completed ? submitted_frames_ - completed : 0;
}
void DiligentPresentation::begin_frame() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (pending_frames() >= 2) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("GPU frame completion timed out; rendering stopped before "
                                     "allocating more in-flight resources");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    poll_gpu();
}
void DiligentPresentation::end_frame(IDeviceContext* context) {
    if (!context || submitted_frames_ == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Invalid presentation frame submission");
    context->EnqueueSignal(frame_fence_, ++submitted_frames_);
    // The host's Present (or offscreen Flush) submits this marker after all
    // frame commands. No raw backend handles or device-wide idle wait.
}
void DiligentPresentation::gpu_thread() const {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("GPU profiling requires the presentation thread");
}
void DiligentPresentation::gpu_services(ServiceAccess services) {
    gpu_thread();
    if (gpu_active_ != gpu_capacity)
        throw std::runtime_error("Cannot rebind profiling during a GPU scope");
    ++gpu_capture_;
    gpu_enabled_ = false;
    gpu_services_ = std::move(services);
}
void DiligentPresentation::gpu_profiling(bool enabled) {
    gpu_thread();
    if (enabled &&
        (!gpu_services_.available(Capability::Profiling) || gpu_failed_ ||
         device_->GetDeviceInfo().Features.DurationQueries != DEVICE_FEATURE_STATE_ENABLED))
        throw std::runtime_error("GPU duration profiling is unavailable on this device");
#ifdef FORGE_DISABLE_PROFILING
    if (enabled)
        throw std::runtime_error("Profiling was disabled in this build");
#endif
    if (enabled != gpu_enabled_) {
        gpu_enabled_ = enabled;
        ++gpu_capture_;
    }
}
nlohmann::json DiligentPresentation::gpu_profile_status() const {
    gpu_thread();
#ifdef FORGE_DISABLE_PROFILING
    constexpr bool compiled = false;
#else
    constexpr bool compiled = true;
#endif
    const bool owner = gpu_services_.available(Capability::Profiling);
    const bool supported =
        device_->GetDeviceInfo().Features.DurationQueries == DEVICE_FEATURE_STATE_ENABLED;
    return {{"compiled", compiled},
            {"available", compiled && supported && owner && !gpu_failed_},
            {"enabled", gpu_enabled_ && owner && !gpu_failed_},
            {"pending", gpu_count_},
            {"query_capacity", gpu_capacity},
            {"created", gpu_created_},
            {"skipped", gpu_skipped_},
            {"stale", gpu_stale_},
            {"latency_frames", gpu_latency_},
            {"submitted_frame", submitted_frames_},
            {"reason", !compiled     ? "Profiling was disabled in this build."
                       : !supported  ? "This device does not support GPU duration queries."
                       : !owner      ? "The profiling service is unavailable."
                       : gpu_failed_ ? "GPU timing failed; rendering remains available."
                                     : ""},
            {"samples",
             owner ? nlohmann::json(gpu_services_.gpu_profiles()) : nlohmann::json::array()}};
}
DiligentPresentation::GpuScope DiligentPresentation::gpu_scope(IDeviceContext* context,
                                                               const char* view, const char* stage,
                                                               bool enabled) {
    if (thread_ != std::this_thread::get_id() || !enabled || !gpu_enabled_ || gpu_failed_ ||
        !gpu_services_.available(Capability::Profiling))
        return {nullptr, nullptr, gpu_capacity};
    if (!context || context->GetDesc().IsDeferred || !view || !stage || !*view || !*stage ||
        std::char_traits<char>::length(view) > 128 || std::char_traits<char>::length(stage) > 128 ||
        gpu_active_ != gpu_capacity || gpu_count_ == gpu_capacity ||
        submitted_frames_ == std::numeric_limits<std::uint64_t>::max()) {
        ++gpu_skipped_;
        return {nullptr, nullptr, gpu_capacity};
    }
    try {
        unsigned slot = 0;
        while (slot < gpu_capacity && gpu_queries_[slot].occupied)
            ++slot;
        auto& entry = gpu_queries_.at(slot);
        entry.view = view;
        entry.stage = stage;
        entry.frame = submitted_frames_ + 1;
        entry.epoch = gpu_services_.profile_epoch();
        entry.capture = gpu_capture_;
        if (!entry.query) {
            QueryDesc desc;
            desc.Name = "FORGE GPU pass duration";
            desc.Type = QUERY_TYPE_DURATION;
            device_->CreateQuery(desc, &entry.query);
            if (!entry.query)
                throw std::runtime_error("GPU duration query creation failed");
            ++gpu_created_;
        }
        entry.occupied = true;
        context->BeginQuery(entry.query);
        gpu_active_ = slot;
        return {this, context, slot};
    } catch (...) {
        gpu_failed_ = true;
        gpu_enabled_ = false;
        return {nullptr, nullptr, gpu_capacity};
    }
}
DiligentPresentation::GpuScope::~GpuScope() {
    if (!owner_)
        return;
    try {
        context_->EndQuery(owner_->gpu_queries_[slot_].query);
        owner_->gpu_pending_[(owner_->gpu_head_ + owner_->gpu_count_) % gpu_capacity] = slot_;
        ++owner_->gpu_count_;
    } catch (...) {
        owner_->gpu_failed_ = true;
        owner_->gpu_enabled_ = false;
    }
    owner_->gpu_active_ = gpu_capacity;
}
void DiligentPresentation::poll_gpu() {
    gpu_thread();
    if (gpu_failed_)
        return; // Keep failed query objects until device-owner teardown; never resubmit them.
    // GetData does not submit or wait. Queries remain owned until the backend
    // reports completion, even after Stop, Clear, service retirement or rebinding.
    while (gpu_count_) {
        auto& entry = gpu_queries_[gpu_pending_[gpu_head_]];
        QueryDataDuration result;
        try {
            if (!entry.query->GetData(&result, sizeof(result))) {
                if (frame_fence_->GetCompletedValue() >= entry.frame) {
                    gpu_failed_ = true;
                    gpu_enabled_ = false;
                }
                break;
            }
        } catch (...) {
            gpu_failed_ = true;
            gpu_enabled_ = false;
            break;
        }
        --gpu_count_;
        gpu_head_ = (gpu_head_ + 1) % gpu_capacity;
        entry.occupied = false;
        if (!gpu_enabled_ || entry.capture != gpu_capture_ ||
            !gpu_services_.available(Capability::Profiling) ||
            entry.epoch != gpu_services_.profile_epoch()) {
            ++gpu_stale_;
            continue;
        }
        if (!result.Frequency) {
            gpu_failed_ = true;
            gpu_enabled_ = false;
            continue;
        }
        try {
            gpu_services_.record_gpu_profile(entry.epoch, entry.view, entry.stage,
                                             double(result.Duration) / double(result.Frequency),
                                             entry.frame);
            gpu_latency_ =
                submitted_frames_ >= entry.frame ? submitted_frames_ - entry.frame + 1 : 0;
        } catch (...) {
            // Optional instrumentation must never change rendering control flow.
            gpu_failed_ = true;
            gpu_enabled_ = false;
        }
    }
}
void DiligentPresentation::clear_cache() {
    cache_->Reset();
    epoch_creations_ = 0;
}
void DiligentPresentation::trim() {
    // Bound retained serialization entries across repeated authoring changes.
    // This is an entry budget, not a claim about a fixed byte/VRAM ceiling.
    if (epoch_creations_ >= 256)
        clear_cache();
}
void DiligentPresentation::shader(const ShaderCreateInfo& info, IShader** result) {
    if (!result)
        throw std::runtime_error("Presentation output pointer is null");
    trim();
    auto selected = info;
    prepare_renderer_shader(device_->GetDeviceInfo(), selected);
    const bool hit = cache_->CreateShader(selected, result);
    if (!result || !*result)
        throw std::runtime_error("Diligent presentation shader creation failed");
    hits_ += hit;
    misses_ += !hit;
    epoch_creations_ += !hit;
}
void DiligentPresentation::graphics(const GraphicsPipelineStateCreateInfo& info,
                                    IPipelineState** result) {
    if (!result)
        throw std::runtime_error("Presentation output pointer is null");
    trim();
    const bool hit = cache_->CreateGraphicsPipelineState(info, result);
    if (!result || !*result)
        throw std::runtime_error("Diligent presentation pipeline creation failed");
    hits_ += hit;
    misses_ += !hit;
    epoch_creations_ += !hit;
}
void DiligentPresentation::compute(const ComputePipelineStateCreateInfo& info,
                                   IPipelineState** result) {
    if (!result)
        throw std::runtime_error("Presentation output pointer is null");
    trim();
    const bool hit = cache_->CreateComputePipelineState(info, result);
    if (!*result)
        throw std::runtime_error("Diligent presentation compute pipeline creation failed");
    hits_ += hit;
    misses_ += !hit;
    epoch_creations_ += !hit;
}
PBR_Renderer& DiligentPresentation::pbr(IDeviceContext* context) {
    if (!context)
        throw std::runtime_error("PBR initialization requires a device context");
    if (!pbr_) {
        PBR_Renderer::CreateInfo info;
        info.EnableSheen = true;
        info.MaxJointCount = 0;
        info.MaxActiveMorphTargetCount = 0;
        info.CreateDefaultJointsBuffer = false;
        info.CreateDefaultMorphTargetBuffer = false;
        info.AllowHotShaderReload = false;
        // Native lighting/LUT/convolution utilities only. FORGE supplies validated
        // signed/zero-safe vertex transforms and per-material resource bindings.
        auto candidate = std::make_unique<PBR_Renderer>(device_, cache_, context, info, false);
        if (!candidate->GetPreintegratedGGX_SRV() || !candidate->GetPreintegratedSheen_SRV() ||
            !candidate->GetWhiteTexSRV() || !candidate->GetDefaultNormalMapSRV())
            throw std::runtime_error("Diligent PBR resource initialization failed");
        pbr_ = std::move(candidate);
    }
    return *pbr_;
}
ITextureView* DiligentPresentation::black_environment(IDeviceContext* context) {
    if (!black_environment_) {
        auto candidate = pbr(context).CreateIrradianceCube(context, "FORGE disabled environment",
                                                           TEX_FORMAT_RGBA16_FLOAT, 1);
        if (!candidate)
            throw std::runtime_error("Default environment allocation failed");
        black_environment_ = std::move(candidate);
    }
    return black_environment_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
ITextureView* DiligentPresentation::empty_shadow(IDeviceContext* context) {
    if (!empty_shadow_) {
        TextureDesc desc;
        desc.Name = "FORGE disabled shadow";
        desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
        desc.Width = desc.Height = desc.ArraySize = 1;
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> candidate;
        device_->CreateTexture(desc, nullptr, &candidate);
        if (!candidate)
            throw std::runtime_error("Disabled shadow allocation failed");
        context->ClearDepthStencil(candidate->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                   CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        empty_shadow_ = std::move(candidate);
    }
    return empty_shadow_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
} // namespace forge
