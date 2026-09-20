#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <forge/asset_ref.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace forge {
enum class ResourceState {
    Unloaded,
    Queued,
    DependencyPending,
    Loading,
    Ready,
    Failed,
    DependencyFailed,
    Cancelled,
    Stale,
    Replacing,
    Retiring
};
const char* resource_state_name(ResourceState state);
struct ResourceMemory {
    std::uint64_t cpu_asset = 0, gpu_texture = 0, gpu_buffer = 0, shader_pipeline = 0,
                  animation = 0;
    std::uint64_t total() const;
    ResourceMemory& operator+=(const ResourceMemory& other);
};
struct ResourceIdentity {
    std::array<std::uint8_t, 16> owner{};
    std::uint64_t slot = 0, generation = 0;
    AssetId asset;
    std::string type, revision;
    auto operator<=>(const ResourceIdentity&) const = default;
};
struct ResourceInfo {
    ResourceIdentity identity;
    std::uint64_t source_generation = 0;
    ResourceState state = ResourceState::Unloaded;
    ResourceMemory memory;
    bool previous_good = false;
    std::string diagnostic;
};
namespace resource_detail {
struct TicketState {
    mutable std::mutex mutex;
    ResourceInfo info;
};
struct Scope {
    const std::array<std::uint8_t, 16> id;
    const std::thread::id thread = std::this_thread::get_id();
    std::atomic<bool> alive = true;
    explicit Scope(std::array<std::uint8_t, 16> id) : id(id) {}
    void check() const;
};
std::array<std::uint8_t, 16> next_scope();
bool terminal(ResourceState state);
void valid_revision(std::string_view value);
} // namespace resource_detail
// A copied observation of one coalesced load, not a lease or a persisted handle.
// Explicit pool cancel/unload cancels that asset's shared request for all observers.
class ResourceTicket {
  public:
    ResourceTicket() = default;
    ResourceInfo inspect() const;
    explicit operator bool() const { return bool(state_); }

  private:
    template <class> friend class ResourcePool;
    explicit ResourceTicket(std::shared_ptr<resource_detail::TicketState> state)
        : state_(std::move(state)) {}
    std::shared_ptr<resource_detail::TicketState> state_;
};
template <class T> struct ResourceTraits;
namespace resource_detail {
template <class T> struct Revision {
    ResourceIdentity identity;
    ResourceMemory memory;
    std::uint64_t source_generation = 0;
    std::shared_ptr<Scope> scope;
    std::weak_ptr<TicketState> ticket;
    std::unique_ptr<const typename ResourceTraits<T>::Data> value;
};
} // namespace resource_detail
template <class T> class WeakResourceLease;
template <class T> class ResourceLease {
  public:
    using Data = typename ResourceTraits<T>::Data;
    ResourceLease() = default;
    explicit operator bool() const { return revision_ && revision_->scope->alive.load(); }
    const Data& get() const {
        if (!revision_)
            throw std::runtime_error("Empty resource lease");
        revision_->scope->check();
        if (!revision_->value)
            throw std::runtime_error("Resource owner has retired this lease");
        return *revision_->value;
    }
    const Data* operator->() const { return &get(); }
    const ResourceIdentity& identity() const {
        if (!revision_)
            throw std::runtime_error("Empty resource lease");
        return revision_->identity;
    }
    WeakResourceLease<T> weak() const { return WeakResourceLease<T>(revision_); }

  private:
    template <class> friend class ResourcePool;
    friend class WeakResourceLease<T>;
    explicit ResourceLease(std::shared_ptr<resource_detail::Revision<T>> revision)
        : revision_(std::move(revision)) {}
    std::shared_ptr<resource_detail::Revision<T>> revision_;
};
template <class T> class WeakResourceLease {
  public:
    WeakResourceLease() = default;
    ResourceLease<T> lock() const {
        const auto scope = scope_.lock();
        if (!scope || !scope->alive.load())
            return {};
        // Check before acquiring any strong revision reference. A rejected
        // off-thread promotion must not race owner-thread final destruction.
        scope->check();
        const auto revision = revision_.lock();
        if (!revision)
            return {};
        return ResourceLease<T>(revision);
    }

  private:
    friend class ResourceLease<T>;
    explicit WeakResourceLease(const std::shared_ptr<resource_detail::Revision<T>>& value)
        : revision_(value), scope_(value ? value->scope : nullptr) {}
    std::weak_ptr<resource_detail::Revision<T>> revision_;
    std::weak_ptr<resource_detail::Scope> scope_;
};
template <class T> struct ResourceResolution {
    ResourceLease<T> lease;
    bool fallback = false;
    ResourceInfo requested;
};
struct ResourcePoolLimits {
    unsigned workers = 2;
    std::size_t requests = 64, assets = 4096;
    std::uint64_t bytes = 512ull * 1024 * 1024;
};
struct ResourceStatistics {
    std::size_t selected = 0, retired = 0, pending = 0;
    ResourceMemory memory;
    std::uint64_t high_water_bytes = 0;
};
template <class T> struct ResourceCandidate {
    std::unique_ptr<typename ResourceTraits<T>::Data> value;
    ResourceMemory memory;
};
// Asset-specific CPU preparation pool. Loaders never touch live worlds or devices.
// Request/inspect are concurrent; adoption, access, unload and retirement are owner
// operations. Physical GPU realization/retirement belongs to the renderer provider.
// Strong leases pin immutable revisions while this owner scope is alive. Close
// revokes the scope and destroys its data on the owner before device/world teardown.
template <class T> class ResourcePool {
  public:
    using Data = typename ResourceTraits<T>::Data;
    using Loader = std::function<ResourceCandidate<T>(std::stop_token)>;
    using Compatibility = std::function<void(const Data&, const Data*)>;
    explicit ResourcePool(ResourcePoolLimits limits = {}, Compatibility compatibility = {})
        : state_(std::make_shared<State>(limits)), compatibility_(std::move(compatibility)) {
        if (!limits.workers || limits.workers > 16 || !limits.requests || limits.requests > 4096 ||
            !limits.assets || limits.assets > 1000000 || !limits.bytes)
            throw std::runtime_error("Invalid resource pool limits");
        try {
            for (unsigned i = 0; i < limits.workers; ++i)
                workers_.emplace_back([state = state_] { state->run(); });
        } catch (...) {
            stop_workers();
            throw;
        }
    }
    ~ResourcePool() {
        if (std::this_thread::get_id() != state_->scope->thread)
            std::terminate();
        close();
    }
    ResourcePool(const ResourcePool&) = delete;
    ResourcePool& operator=(const ResourcePool&) = delete;
    ResourceTicket request(AssetRef<T> asset, std::string revision, std::uint64_t source_generation,
                           Loader loader, std::vector<ResourceTicket> dependencies = {},
                           int priority = 0) {
        if (!asset.id || !loader || !source_generation || dependencies.size() > 64)
            throw std::runtime_error("Invalid resource request");
        resource_detail::valid_revision(revision);
        for (const auto& d : dependencies)
            if (!d)
                throw std::runtime_error("Empty resource dependency");
        std::lock_guard lock(state_->mutex);
        if (state_->shutdown)
            throw std::runtime_error("Resource owner is closed");
        auto found = state_->slots.find(asset.id);
        if (found != state_->slots.end() && found->second.request) {
            const auto info = ResourceTicket(found->second.request).inspect();
            if (source_generation < info.source_generation ||
                (source_generation == info.source_generation && revision != info.identity.revision))
                throw std::runtime_error("Stale/conflicting resource source generation");
            if (source_generation == info.source_generation &&
                (!resource_detail::terminal(info.state) || info.state == ResourceState::Ready))
                return ResourceTicket(found->second.request);
        }
        if (state_->jobs.size() >= state_->limits.requests ||
            (found == state_->slots.end() && state_->slots.size() >= state_->limits.assets))
            throw std::runtime_error("Resource request/asset capacity exhausted");
        if (state_->next == UINT64_MAX)
            throw std::runtime_error("Resource generation exhausted");
        auto& slot = state_->slots[asset.id];
        if (!slot.slot)
            slot.slot = state_->next;
        if (slot.request) {
            auto previous = slot.request;
            for (auto& job : state_->jobs)
                if (job->ticket == previous) {
                    job->stop.request_stop();
                    state_->status(*job, ResourceState::Stale, "Superseded resource request");
                }
        }
        auto job = std::make_shared<Job>();
        job->loader = std::move(loader);
        job->dependencies = std::move(dependencies);
        job->priority = priority;
        job->ticket = std::make_shared<resource_detail::TicketState>();
        job->ticket->info = {
            {state_->scope->id, slot.slot, state_->next++, asset.id, T::type, std::move(revision)},
            source_generation,
            ResourceState::Queued,
            {},
            bool(slot.current),
            {}};
        slot.request = job->ticket;
        state_->jobs.push_back(job);
        state_->changed.notify_all();
        return ResourceTicket(job->ticket);
    }
    // Calls compatibility before adoption, then rechecks generation/cancellation.
    std::size_t pump() {
        state_->scope->check();
        if (pumping_)
            throw std::runtime_error("Resource adoption cannot re-enter itself");
        pumping_ = true;
        struct Guard {
            bool& value;
            ~Guard() { value = false; }
        } guard{pumping_};
        std::size_t adopted = 0;
        for (;;) {
            std::shared_ptr<Job> job;
            std::shared_ptr<resource_detail::Revision<T>> prior;
            {
                std::lock_guard lock(state_->mutex);
                auto it = std::find_if(state_->jobs.begin(), state_->jobs.end(),
                                       [](const auto& j) { return j->complete; });
                if (it == state_->jobs.end())
                    break;
                job = *it;
                const auto id = ResourceTicket(job->ticket).inspect().identity.asset;
                prior = state_->slots.at(id).current;
            }
            std::string failure;
            if (job->candidate.value && compatibility_ && !job->stop.stop_requested()) {
                try {
                    compatibility_(*job->candidate.value, prior ? prior->value.get() : nullptr);
                } catch (const std::exception& e) {
                    failure = std::string(e.what()).substr(0, 8192);
                    if (failure.empty())
                        failure = "Resource compatibility failed";
                } catch (...) {
                    failure = "Resource compatibility failed";
                }
            }
            std::lock_guard lock(state_->mutex);
            const auto info = ResourceTicket(job->ticket).inspect();
            auto& slot = state_->slots.at(info.identity.asset);
            bool dependency_failed = false;
            for (const auto& dependency : job->dependencies) {
                const auto value = dependency.inspect();
                if (value.state != ResourceState::Ready) {
                    dependency_failed = true;
                    failure = "Dependency changed before adoption: " + value.identity.asset.str() +
                              " / " + value.identity.revision + ": " + value.diagnostic;
                    break;
                }
            }
            if (slot.request != job->ticket)
                state_->status(*job, ResourceState::Stale, "Late resource completion discarded");
            else if (job->stop.stop_requested())
                state_->status(*job, ResourceState::Cancelled, "Resource request cancelled");
            else if (!failure.empty())
                state_->status(*job,
                               dependency_failed ? ResourceState::DependencyFailed
                                                 : ResourceState::Failed,
                               std::move(failure));
            else if (job->candidate.value) {
                auto next = std::make_shared<resource_detail::Revision<T>>();
                next->identity = info.identity;
                next->source_generation = info.source_generation;
                next->scope = state_->scope;
                next->ticket = job->ticket;
                next->memory = job->candidate.memory;
                next->value = std::move(job->candidate.value);
                if (slot.current) {
                    State::revision_status(slot.current, ResourceState::Retiring);
                    state_->retired.push_back(std::move(slot.current));
                }
                slot.current = std::move(next);
                slot.last_use = ++state_->clock;
                state_->status(*job, ResourceState::Ready, {});
                ++adopted;
                std::lock_guard ticket_lock(job->ticket->mutex);
                job->ticket->info.memory = slot.current->memory;
                job->ticket->info.previous_good = false;
            }
            std::erase(state_->jobs, job);
            state_->changed.notify_all();
        }
        collect();
        return adopted;
    }
    ResourceLease<T> current(AssetRef<T> asset) {
        state_->scope->check();
        std::lock_guard lock(state_->mutex);
        const auto found = state_->slots.find(asset.id);
        if (found == state_->slots.end() || !found->second.current)
            return {};
        found->second.last_use = ++state_->clock;
        return ResourceLease<T>(found->second.current);
    }
    ResourceLease<T> acquire(const ResourceTicket& request) {
        state_->scope->check();
        const auto info = request.inspect();
        if (info.identity.owner != state_->scope->id || info.identity.type != T::type)
            throw std::runtime_error("Resource request belongs to another owner/type");
        auto lease = current({info.identity.asset});
        return lease && lease.identity() == info.identity ? lease : ResourceLease<T>{};
    }
    ResourceInfo inspect(AssetRef<T> asset) const {
        state_->scope->check();
        std::lock_guard lock(state_->mutex);
        const auto found = state_->slots.find(asset.id);
        if (found != state_->slots.end() && found->second.request)
            return ResourceTicket(found->second.request).inspect();
        ResourceInfo info;
        info.identity.asset = asset.id;
        info.identity.type = T::type;
        info.identity.owner = state_->scope->id;
        return info;
    }
    ResourceResolution<T> resolve(AssetRef<T> asset, ResourceLease<T> fallback = {}) {
        ResourceResolution<T> result{current(asset), false, inspect(asset)};
        if (!result.lease && fallback) {
            (void)fallback.get();
            result.lease = std::move(fallback);
            result.fallback = true;
        }
        return result;
    }
    std::vector<ResourceInfo> revisions() const {
        state_->scope->check();
        std::lock_guard lock(state_->mutex);
        std::vector<ResourceInfo> result;
        auto add = [&](const auto& r, ResourceState status) {
            result.push_back({r->identity, r->source_generation, status, r->memory, false, {}});
        };
        for (const auto& [id, slot] : state_->slots) {
            (void)id;
            if (slot.current)
                add(slot.current, ResourceState::Ready);
        }
        for (const auto& r : state_->retired)
            add(r, ResourceState::Retiring);
        return result;
    }
    // Explicit tooling wait. A timeout does not cancel a shared request implicitly.
    bool wait(const ResourceTicket& ticket, std::chrono::milliseconds timeout) {
        state_->scope->check();
        if (ticket.inspect().identity.owner != state_->scope->id)
            throw std::runtime_error("Foreign resource request");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            pump();
            const auto info = ticket.inspect();
            if (resource_detail::terminal(info.state))
                return info.state == ResourceState::Ready;
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::unique_lock lock(state_->mutex);
            state_->changed.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() +
                                                                    std::chrono::milliseconds(10)));
        }
    }
    void cancel(AssetRef<T> asset) {
        state_->scope->check();
        std::lock_guard lock(state_->mutex);
        for (auto& job : state_->jobs)
            if (ResourceTicket(job->ticket).inspect().identity.asset == asset.id) {
                job->stop.request_stop();
                state_->status(*job, ResourceState::Cancelled, "Resource request cancelled");
            }
        state_->changed.notify_all();
    }
    void unload(AssetRef<T> asset) {
        cancel(asset);
        std::lock_guard lock(state_->mutex);
        const auto found = state_->slots.find(asset.id);
        if (found == state_->slots.end())
            return;
        auto& slot = found->second;
        const auto prior_ticket = slot.current ? slot.current->ticket.lock() : nullptr;
        if (slot.current) {
            State::revision_status(slot.current, ResourceState::Retiring);
            state_->retired.push_back(std::move(slot.current));
        }
        if (slot.request && slot.request != prior_ticket) {
            std::lock_guard ticket_lock(slot.request->mutex);
            slot.request->info.state = ResourceState::Unloaded;
            slot.request->info.previous_good = false;
        }
        // Detach request identity so any in-flight completion remains stale.
        slot.request.reset();
    }
    void collect() {
        state_->scope->check();
        std::lock_guard lock(state_->mutex);
        std::erase_if(state_->retired, [](const auto& revision) {
            if (revision.use_count() != 1)
                return false;
            State::revision_status(revision, ResourceState::Unloaded);
            return true;
        });
        std::erase_if(state_->slots, [&](const auto& entry) {
            if (entry.second.current ||
                (entry.second.request &&
                 ResourceTicket(entry.second.request).inspect().state != ResourceState::Unloaded))
                return false;
            return std::none_of(state_->jobs.begin(), state_->jobs.end(), [&](const auto& job) {
                return ResourceTicket(job->ticket).inspect().identity.asset == entry.first;
            });
        });
    }
    std::size_t evict_idle(std::uint64_t target_bytes) {
        state_->scope->check();
        collect();
        std::lock_guard lock(state_->mutex);
        std::vector<Slot*> candidates;
        for (auto& [id, slot] : state_->slots) {
            (void)id;
            if (slot.current && slot.current.use_count() == 1 &&
                (!slot.request ||
                 ResourceTicket(slot.request).inspect().state == ResourceState::Ready))
                candidates.push_back(&slot);
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](auto a, auto b) { return a->last_use < b->last_use; });
        std::size_t count = 0;
        for (auto* slot : candidates) {
            if (state_->statistics().memory.total() <= target_bytes)
                break;
            State::revision_status(slot->current, ResourceState::Unloaded);
            slot->current.reset();
            ++count;
            if (slot->request) {
                std::lock_guard ticket_lock(slot->request->mutex);
                slot->request->info.state = ResourceState::Unloaded;
                slot->request->info.memory = {};
            }
        }
        return count;
    }
    ResourceStatistics statistics() const {
        state_->scope->check();
        std::lock_guard lock(state_->mutex);
        return state_->statistics();
    }
    void close() {
        if (!state_->scope->alive.load())
            return;
        state_->scope->check();
        if (pumping_)
            throw std::runtime_error("Cannot close a resource owner during adoption");
        stop_workers();
        std::lock_guard lock(state_->mutex);
        state_->scope->alive.store(false);
        for (auto& [id, slot] : state_->slots) {
            (void)id;
            if (slot.current) {
                State::revision_status(slot.current, ResourceState::Unloaded);
                slot.current->value.reset();
            }
            if (slot.request) {
                std::lock_guard ticket_lock(slot.request->mutex);
                slot.request->info.state = ResourceState::Unloaded;
            }
        }
        for (auto& revision : state_->retired) {
            State::revision_status(revision, ResourceState::Unloaded);
            revision->value.reset();
        }
        state_->slots.clear();
        state_->retired.clear();
        state_->jobs.clear();
    }

  private:
    struct Job {
        std::shared_ptr<resource_detail::TicketState> ticket;
        Loader loader;
        std::vector<ResourceTicket> dependencies;
        int priority = 0;
        std::stop_source stop;
        bool executing = false, complete = false;
        ResourceCandidate<T> candidate;
    };
    struct Slot {
        std::uint64_t slot = 0, last_use = 0;
        std::shared_ptr<resource_detail::TicketState> request;
        std::shared_ptr<resource_detail::Revision<T>> current;
    };
    struct State {
        explicit State(ResourcePoolLimits limits)
            : limits(limits),
              scope(std::make_shared<resource_detail::Scope>(resource_detail::next_scope())) {}
        ResourcePoolLimits limits;
        std::shared_ptr<resource_detail::Scope> scope;
        mutable std::mutex mutex;
        std::condition_variable changed;
        std::map<AssetId, Slot> slots;
        std::vector<std::shared_ptr<Job>> jobs;
        std::vector<std::shared_ptr<resource_detail::Revision<T>>> retired;
        std::uint64_t next = 1, clock = 0, high_water = 0;
        bool shutdown = false;
        static void revision_status(const std::shared_ptr<resource_detail::Revision<T>>& revision,
                                    ResourceState state) {
            if (auto ticket = revision->ticket.lock()) {
                std::lock_guard lock(ticket->mutex);
                ticket->info.state = state;
                if (state == ResourceState::Unloaded)
                    ticket->info.memory = {};
            }
        }
        void status(Job& job, ResourceState state, std::string diagnostic) {
            std::lock_guard lock(job.ticket->mutex);
            job.ticket->info.state = state;
            diagnostic.resize(std::min<std::size_t>(8192, diagnostic.size()));
            job.ticket->info.diagnostic = std::move(diagnostic);
        }
        ResourceStatistics statistics() const {
            ResourceStatistics result;
            result.retired = retired.size();
            result.pending = jobs.size();
            for (const auto& [id, slot] : slots) {
                (void)id;
                if (slot.current) {
                    ++result.selected;
                    result.memory += slot.current->memory;
                }
            }
            for (const auto& revision : retired)
                result.memory += revision->memory;
            for (const auto& job : jobs)
                if (job->candidate.value)
                    result.memory += job->candidate.memory;
            result.high_water_bytes = high_water;
            return result;
        }
        void run() {
            for (;;) {
                std::shared_ptr<Job> selected;
                {
                    std::unique_lock lock(mutex);
                    for (;;) {
                        if (shutdown)
                            return;
                        for (auto& job : jobs) {
                            if (job->executing || job->complete)
                                continue;
                            if (job->stop.stop_requested()) {
                                job->complete = true;
                                continue;
                            }
                            bool pending = false, failed = false;
                            for (const auto& dependency : job->dependencies) {
                                const auto info = dependency.inspect();
                                if (info.state == ResourceState::Ready)
                                    continue;
                                if (resource_detail::terminal(info.state)) {
                                    status(*job, ResourceState::DependencyFailed,
                                           "Dependency " + info.identity.asset.str() + " / " +
                                               info.identity.revision + ": " + info.diagnostic);
                                    job->complete = true;
                                    failed = true;
                                    break;
                                }
                                pending = true;
                            }
                            if (failed)
                                continue;
                            if (pending) {
                                status(*job, ResourceState::DependencyPending, {});
                                continue;
                            }
                            if (!selected || job->priority > selected->priority)
                                selected = job;
                        }
                        if (selected)
                            break;
                        changed.wait_for(lock, std::chrono::milliseconds(20));
                    }
                    selected->executing = true;
                    status(*selected,
                           ResourceTicket(selected->ticket).inspect().previous_good
                               ? ResourceState::Replacing
                               : ResourceState::Loading,
                           {});
                }
                ResourceCandidate<T> candidate;
                std::string failure;
                try {
                    candidate = selected->loader(selected->stop.get_token());
                    if (!candidate.value)
                        throw std::runtime_error("Resource loader returned no value");
                    (void)candidate.memory.total();
                } catch (const std::exception& e) {
                    failure = std::string(e.what()).substr(0, 8192);
                    if (failure.empty())
                        failure = "Resource load failed";
                } catch (...) {
                    failure = "Resource load failed with nonstandard exception";
                }
                {
                    std::lock_guard lock(mutex);
                    selected->loader = {};
                    selected->executing = false;
                    selected->complete = true;
                    const auto info = ResourceTicket(selected->ticket).inspect();
                    if (selected->stop.stop_requested() || shutdown) {
                        if (info.state != ResourceState::Stale)
                            status(*selected, ResourceState::Cancelled,
                                   "Resource request cancelled");
                    } else if (!failure.empty())
                        status(*selected, ResourceState::Failed, std::move(failure));
                    else if (candidate.memory.total() > limits.bytes ||
                             statistics().memory.total() > limits.bytes - candidate.memory.total())
                        status(*selected, ResourceState::Failed,
                               "Resource memory budget exhausted; release leases or evict idle "
                               "resources");
                    else {
                        selected->candidate = std::move(candidate);
                        high_water = std::max(high_water, statistics().memory.total());
                    }
                    changed.notify_all();
                }
            }
        }
    };
    void stop_workers() {
        {
            std::lock_guard lock(state_->mutex);
            state_->shutdown = true;
            for (auto& job : state_->jobs)
                job->stop.request_stop();
            state_->changed.notify_all();
        }
        for (auto& worker : workers_)
            if (worker.joinable())
                worker.join();
        workers_.clear();
    }
    std::shared_ptr<State> state_;
    Compatibility compatibility_;
    bool pumping_ = false;
    std::vector<std::thread> workers_;
};
} // namespace forge
