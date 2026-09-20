#pragma once
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "mesh_gpu.hpp"
#include "texture_gpu.hpp"
#include <forge/mesh_resource.hpp>
#include <forge/texture_resource.hpp>
#include <limits>
#include <map>
namespace forge {
namespace gpu_detail {
struct Scope {
    const std::thread::id thread = std::this_thread::get_id();
    std::atomic<bool> alive = true;
    void check() const {
        if (!alive.load() || thread != std::this_thread::get_id())
            throw std::runtime_error(
                "GPU resource scope is closed or accessed from another thread");
    }
};
template <class T> struct Traits;
template <> struct Traits<MeshAsset> {
    using Data = GpuMesh;
    static std::uint64_t bytes(const MeshResourceData&);
    static Data upload(Diligent::IRenderDevice* device, const MeshResourceData& value) {
        return upload_mesh(device, value.mesh);
    }
};
template <> struct Traits<TextureAsset> {
    using Data = Diligent::RefCntAutoPtr<Diligent::ITexture>;
    static std::uint64_t bytes(const TextureData& value) {
        validate_texture(value);
        return value.byte_size();
    }
    static Data upload(Diligent::IRenderDevice* device, const TextureData& value) {
        return upload_texture(device, value);
    }
};
template <class T> struct Revision {
    ResourceIdentity source;
    std::shared_ptr<Scope> scope;
    std::unique_ptr<typename Traits<T>::Data> value;
    std::uint64_t bytes = 0, last_fence = 0, last_use = 0;
    bool pending_submission = true, retiring = false;
};
} // namespace gpu_detail
template <class T> class GpuResidency;
// Private physical realization of an existing CPU revision, not an AssetId
// resolver or new persistent handle. Native binding bundles must retain this
// lease until their SRBs/buffers are destroyed; close bundles before this owner.
template <class T> class GpuLease {
  public:
    GpuLease() = default;
    explicit operator bool() const { return revision_ && revision_->scope->alive.load(); }
    const typename gpu_detail::Traits<T>::Data& get() const {
        if (!revision_)
            throw std::runtime_error("Empty GPU lease");
        revision_->scope->check();
        if (!revision_->value)
            throw std::runtime_error("Retired GPU resource");
        revision_->pending_submission = true;
        return *revision_->value;
    }
    const ResourceIdentity& source() const {
        (void)get();
        return revision_->source;
    }

  private:
    friend class GpuResidency<T>;
    explicit GpuLease(std::shared_ptr<gpu_detail::Revision<T>> revision)
        : revision_(std::move(revision)) {}
    std::shared_ptr<gpu_detail::Revision<T>> revision_;
};
struct GpuResidencyStats {
    std::size_t resident = 0, retiring = 0;
    // Requested payload bytes, including in-flight retirement and conservative
    // full reservations for partially failed native uploads.
    // Driver allocation padding, descriptor heaps and shared pages are not VRAM estimates.
    std::uint64_t payload_bytes = 0, high_water_bytes = 0, submitted_fence = 0, completed_fence = 0;
};
template <class T> class GpuResidency {
  public:
    GpuResidency(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                 std::uint64_t bytes, std::size_t resources = 4096)
        : device_(device), context_(context), budget_(bytes), resource_limit_(resources),
          scope_(std::make_shared<gpu_detail::Scope>()) {
        if (!device || !context || !bytes || !resources || resources > 1000000)
            throw std::runtime_error("GPU residency needs a device, context and budget");
        const auto& queue = context->GetDesc();
        if (queue.IsDeferred || queue.ContextId != 0 ||
            (queue.QueueType & Diligent::COMMAND_QUEUE_TYPE_PRIMARY_MASK) !=
                Diligent::COMMAND_QUEUE_TYPE_GRAPHICS)
            throw std::runtime_error(
                "GPU residency requires the owning primary immediate graphics context");
        Diligent::FenceDesc desc;
        desc.Name = "FORGE physical resource retirement";
        device_->CreateFence(desc, &fence_);
        if (!fence_)
            throw std::runtime_error("GPU retirement fence creation failed");
    }
    ~GpuResidency() {
        if (scope_->alive.load()) {
            try {
                close();
            } catch (...) {
                std::terminate();
            }
        }
    }
    GpuResidency(const GpuResidency&) = delete;
    GpuResidency& operator=(const GpuResidency&) = delete;
    GpuLease<T> acquire(const ResourceLease<T>& source) {
        scope_->check();
        const auto& data = source.get(); // Validate CPU scope/thread before identity lookup.
        const auto& id = source.identity();
        if (auto found = entries_.find(id); found != entries_.end()) {
            auto& value = *found->second;
            value.pending_submission = true;
            value.retiring = false;
            value.last_use = ++use_;
            return GpuLease<T>(found->second);
        }
        collect();
        const auto bytes = gpu_detail::Traits<T>::bytes(data);
        if (bytes > budget_)
            throw std::runtime_error("GPU resource payload exceeds owner budget");
        // Evict only idle completed revisions. Leased or submitted candidates
        // remain accounted; no fake frame countdown or overwrite of old buffers.
        while (bytes > budget_ - retained_ || entries_.size() + failed_.size() >= resource_limit_) {
            auto oldest = entries_.end();
            const auto completed = fence_->GetCompletedValue();
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                const auto& value = *it->second;
                if (it->second.use_count() == 1 && !value.pending_submission &&
                    value.last_fence <= completed &&
                    (oldest == entries_.end() || value.last_use < oldest->second->last_use))
                    oldest = it;
            }
            if (oldest == entries_.end())
                throw std::runtime_error(
                    "GPU byte/count budget is held by leased or in-flight resources");
            retained_ -= oldest->second->bytes;
            entries_.erase(oldest);
        }
        if (entries_.size() + failed_.size() >= resource_limit_)
            throw std::runtime_error("GPU resource count budget exceeded");
        auto candidate = std::make_shared<gpu_detail::Revision<T>>();
        candidate->source = id;
        candidate->scope = scope_;
        candidate->bytes = bytes;
        candidate->last_use = ++use_;
        // Reserve before native allocation: a failed partial upload may remain
        // in Diligent release queues until the submission completes.
        failed_.push_back({bytes, 0});
        retained_ += bytes;
        high_water_ = std::max(high_water_, retained_);
        candidate->value = std::make_unique<typename gpu_detail::Traits<T>::Data>(
            gpu_detail::Traits<T>::upload(device_, data));
        const auto [it, inserted] = entries_.emplace(id, candidate);
        if (!inserted)
            throw std::runtime_error("GPU candidate publication identity collision");
        failed_.pop_back();
        return GpuLease<T>(std::move(candidate));
    }
    void unload(const ResourceIdentity& source) {
        scope_->check();
        if (auto found = entries_.find(source); found != entries_.end())
            found->second->retiring = true;
        collect();
    }
    // Call after all draws that used acquisitions in this submission. Diligent
    // signals only when context work is flushed; flush is explicit here.
    void submit() {
        scope_->check();
        if (submitted_ == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("GPU fence sequence exhausted");
        context_->EnqueueSignal(fence_, ++submitted_);
        context_->Flush();
        for (auto& failure : failed_)
            if (!failure.fence)
                failure.fence = submitted_;
        for (auto& [id, entry] : entries_) {
            (void)id;
            if (entry->pending_submission) {
                entry->last_fence = submitted_;
                entry->pending_submission = false;
            }
        }
    }
    void collect() {
        scope_->check();
        const auto completed = fence_->GetCompletedValue();
        for (auto it = failed_.begin(); it != failed_.end();) {
            if (it->fence && it->fence <= completed) {
                retained_ -= it->bytes;
                it = failed_.erase(it);
            } else
                ++it;
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            const auto& value = *it->second;
            if (value.retiring && it->second.use_count() == 1 && !value.pending_submission &&
                value.last_fence <= completed) {
                retained_ -= value.bytes;
                it = entries_.erase(it);
            } else
                ++it;
        }
    }
    GpuResidencyStats statistics() const {
        scope_->check();
        GpuResidencyStats result;
        result.retiring = failed_.size();
        result.payload_bytes = retained_;
        result.high_water_bytes = high_water_;
        result.submitted_fence = submitted_;
        result.completed_fence = fence_->GetCompletedValue();
        for (const auto& [id, value] : entries_) {
            (void)id;
            if (value->retiring)
                ++result.retiring;
            else
                ++result.resident;
        }
        return result;
    }
    void close() {
        if (!scope_->alive.load())
            return;
        scope_->check();
        submit();
        fence_->Wait(submitted_);
        scope_->alive = false;
        for (auto& [id, value] : entries_) {
            (void)id;
            value->value.reset();
        }
        entries_.clear();
        failed_.clear();
        retained_ = 0;
        fence_.Release();
        context_.Release();
        device_.Release();
    }

  private:
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    Diligent::RefCntAutoPtr<Diligent::IFence> fence_;
    std::uint64_t budget_, retained_ = 0, high_water_ = 0, submitted_ = 0, use_ = 0;
    const std::size_t resource_limit_;
    struct FailedReservation {
        std::uint64_t bytes, fence;
    };
    std::vector<FailedReservation> failed_;
    std::shared_ptr<gpu_detail::Scope> scope_;
    std::map<ResourceIdentity, std::shared_ptr<gpu_detail::Revision<T>>> entries_;
};
} // namespace forge
