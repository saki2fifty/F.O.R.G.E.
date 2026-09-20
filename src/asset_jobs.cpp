#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <forge/asset_jobs.hpp>
#include <limits>
#include <mutex>
#include <thread>

namespace forge {
namespace {
bool finished(AssetJobState state) {
    return state == AssetJobState::Ready || state == AssetJobState::Failed ||
           state == AssetJobState::Cancelled || state == AssetJobState::Stale;
}
} // namespace
struct AssetBuildQueue::State : std::enable_shared_from_this<AssetBuildQueue::State> {
    struct Job {
        AssetJobInfo info;
        Task task;
        std::stop_source stop;
        std::shared_ptr<const CachedArtifact> artifact;
        bool delivered = false;
        bool executing = false;
    };
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<AssetJobId, std::shared_ptr<Job>> jobs;
    std::map<AssetId, std::uint64_t> generations;
    std::vector<std::thread> workers;
    std::size_t capacity;
    std::size_t byte_budget;
    std::size_t retained_bytes = 0;
    AssetJobId next = 1;
    bool shutdown = false;

    State(std::size_t capacity, std::size_t byte_budget)
        : capacity(capacity), byte_budget(byte_budget) {}
    std::shared_ptr<Job> select() {
        std::shared_ptr<Job> best;
        for (auto& [id, job] : jobs) {
            (void)id;
            if (finished(job->info.state) || job->executing)
                continue;
            bool waiting = false;
            for (auto dependency : job->info.dependencies) {
                const auto& parent = jobs.at(dependency);
                if (generations.at(parent->info.asset) != parent->info.generation) {
                    job->info.state = AssetJobState::Stale;
                    job->info.diagnostic = "A prerequisite asset generation was superseded";
                    job->task = {};
                    changed.notify_all();
                    break;
                }
                if (parent->info.state == AssetJobState::Ready)
                    continue;
                if (finished(parent->info.state)) {
                    job->info.state = AssetJobState::Failed;
                    job->info.diagnostic =
                        "Dependency job " + std::to_string(dependency) +
                        " did not complete successfully: " + parent->info.diagnostic;
                    job->info.diagnostic.resize(
                        std::min<std::size_t>(8192, job->info.diagnostic.size()));
                    job->task = {};
                    changed.notify_all();
                    break;
                }
                waiting = true;
            }
            if (finished(job->info.state))
                continue;
            job->info.state = waiting ? AssetJobState::Waiting : AssetJobState::Queued;
            if (!waiting && (!best || job->info.priority > best->info.priority))
                best = job;
        }
        return best;
    }
    void run() {
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] {
                    job = select();
                    return shutdown || bool(job);
                });
                if (shutdown)
                    return;
                job->executing = true;
                job->info.state = AssetJobState::Running;
            }
            std::shared_ptr<const CachedArtifact> artifact;
            std::string failure;
            try {
                const std::weak_ptr<State> owner = shared_from_this();
                const std::weak_ptr<Job> target = job;
                auto result = job->task(
                    job->stop.get_token(), [owner, target](double value, std::string stage) {
                        if (!std::isfinite(value) || value < 0 || value > 1 || stage.size() > 1024)
                            throw std::runtime_error("Invalid asset job progress");
                        auto state = owner.lock();
                        auto active = target.lock();
                        if (!state || !active)
                            return;
                        std::lock_guard lock(state->mutex);
                        if (active->executing && !finished(active->info.state)) {
                            active->info.progress = value;
                            active->info.stage = std::move(stage);
                        }
                    });
                if (result.key != job->info.build_key)
                    throw std::runtime_error(
                        "Worker returned an artifact for different build inputs");
                artifact = std::make_shared<const CachedArtifact>(std::move(result));
            } catch (const std::exception& e) {
                failure = std::string(e.what()).substr(0, 8192);
                if (failure.empty())
                    failure = "Asset build task failed without a diagnostic";
            } catch (...) {
                failure = "Asset build task failed with a nonstandard exception";
            }
            {
                std::lock_guard lock(mutex);
                job->executing = false;
                job->task = {};
                const bool stale_dependency = std::any_of(
                    job->info.dependencies.begin(), job->info.dependencies.end(),
                    [&](AssetJobId id) {
                        const auto& parent = jobs.at(id);
                        return parent->info.state != AssetJobState::Ready ||
                               generations.at(parent->info.asset) != parent->info.generation;
                    });
                if (generations.at(job->info.asset) != job->info.generation || stale_dependency) {
                    job->info.state = AssetJobState::Stale;
                    job->info.diagnostic =
                        "A newer source/settings/prerequisite generation superseded this result";
                } else if (job->stop.stop_requested() ||
                           job->info.state == AssetJobState::Cancelled) {
                    job->info.state = AssetJobState::Cancelled;
                    job->info.diagnostic = "Asset job cancelled";
                } else if (!failure.empty()) {
                    job->info.state = AssetJobState::Failed;
                    job->info.diagnostic = std::move(failure);
                } else {
                    if (artifact->byte_size() > byte_budget - retained_bytes) {
                        job->info.state = AssetJobState::Failed;
                        job->info.diagnostic =
                            "Completed asset memory budget exhausted; drain results and retry";
                    } else {
                        job->info.state = AssetJobState::Ready;
                        job->info.progress = 1;
                        retained_bytes += artifact->byte_size();
                        job->artifact = std::move(artifact);
                    }
                }
            }
            changed.notify_all();
        }
    }
};
AssetBuildQueue::AssetBuildQueue(unsigned workers, std::size_t capacity, std::size_t byte_budget)
    : state_(std::make_shared<State>(capacity, byte_budget)) {
    if (!workers || workers > 16 || !capacity || capacity > 4096 || !byte_budget ||
        byte_budget > std::uint64_t(2) * 1024 * 1024 * 1024)
        throw std::runtime_error("Invalid asset queue worker/capacity limit");
    try {
        for (unsigned i = 0; i < workers; ++i)
            state_->workers.emplace_back([state = state_] { state->run(); });
    } catch (...) {
        {
            std::lock_guard lock(state_->mutex);
            state_->shutdown = true;
        }
        state_->changed.notify_all();
        for (auto& thread : state_->workers)
            thread.join();
        throw;
    }
}
AssetBuildQueue::~AssetBuildQueue() {
    std::vector<std::stop_source> cancellations;
    {
        std::lock_guard lock(state_->mutex);
        state_->shutdown = true;
        for (auto& [id, job] : state_->jobs) {
            (void)id;
            cancellations.push_back(job->stop);
        }
    }
    // stop callbacks may execute inline; never invoke trusted task code while holding mutex.
    for (auto& stop : cancellations)
        stop.request_stop();
    state_->changed.notify_all();
    for (auto& worker : state_->workers)
        worker.join();
}
AssetJobId AssetBuildQueue::submit(AssetId asset, std::uint64_t generation, std::string key,
                                   int priority, std::vector<AssetJobId> dependencies, Task task) {
    if (!asset || !generation || !valid_content_digest(key) || !task)
        throw std::runtime_error("Invalid asset build request");
    std::unique_lock lock(state_->mutex);
    if (state_->shutdown)
        throw std::runtime_error("Asset build queue is shutting down");
    const auto current = state_->generations.find(asset);
    if (current == state_->generations.end() && state_->generations.size() >= 100000)
        throw std::runtime_error("Asset queue exceeds 100000 logical assets; reopen the project");
    if (current != state_->generations.end() && generation < current->second)
        throw std::runtime_error("Stale asset build generation");
    std::sort(dependencies.begin(), dependencies.end());
    if (std::adjacent_find(dependencies.begin(), dependencies.end()) != dependencies.end())
        throw std::runtime_error("Duplicate asset job dependency");
    for (auto dependency : dependencies)
        if (!state_->jobs.contains(dependency))
            throw std::runtime_error("Unknown asset job dependency");
    for (auto& [id, job] : state_->jobs)
        if (job->info.asset == asset && job->info.generation == generation &&
            job->info.build_key == key && !finished(job->info.state)) {
            if (job->info.dependencies != dependencies)
                throw std::runtime_error("Coalesced job dependencies disagree");
            job->info.priority = std::max(priority, job->info.priority);
            return id;
        }
    // Keep receipts until drained, and completed prerequisites until their dependents finish.
    std::set<AssetJobId> referenced;
    referenced.insert(dependencies.begin(), dependencies.end());
    for (const auto& [id, job] : state_->jobs) {
        (void)id;
        if (!job->delivered || job->executing)
            referenced.insert(job->info.dependencies.begin(), job->info.dependencies.end());
    }
    std::erase_if(state_->jobs, [&](const auto& item) {
        return item.second->delivered && !item.second->executing &&
               !referenced.contains(item.first);
    });
    if (state_->jobs.size() >= state_->capacity ||
        state_->next == std::numeric_limits<AssetJobId>::max())
        throw std::runtime_error("Asset queue capacity reached; drain completions before retrying");
    auto job = std::make_shared<State::Job>();
    job->info = {state_->next++,
                 asset,
                 generation,
                 std::move(key),
                 AssetJobState::Queued,
                 priority,
                 0,
                 {},
                 {},
                 std::move(dependencies)};
    job->task = std::move(task);
    state_->jobs.emplace(job->info.id, job);
    state_->generations[asset] = generation;
    std::vector<std::stop_source> cancellations;
    for (auto& [id, older] : state_->jobs) {
        (void)id;
        if (older->info.asset == asset && older->info.generation < generation &&
            !older->delivered) {
            older->info.state = AssetJobState::Stale;
            older->info.diagnostic = "A newer asset generation superseded this job";
            if (older->artifact)
                state_->retained_bytes -= older->artifact->byte_size();
            older->artifact.reset();
            if (!older->executing)
                older->task = {};
            cancellations.push_back(older->stop);
        }
    }
    lock.unlock();
    for (auto& stop : cancellations)
        stop.request_stop();
    state_->changed.notify_all();
    return job->info.id;
}
void AssetBuildQueue::cancel(AssetJobId id) {
    std::unique_lock lock(state_->mutex);
    const auto it = state_->jobs.find(id);
    if (it == state_->jobs.end())
        throw std::runtime_error("Unknown asset job");
    auto& job = it->second;
    if (job->delivered)
        return;
    auto stop = job->stop;
    job->info.state = AssetJobState::Cancelled;
    job->info.diagnostic = "Asset job cancelled";
    if (job->artifact)
        state_->retained_bytes -= job->artifact->byte_size();
    job->artifact.reset();
    if (!job->executing)
        job->task = {};
    lock.unlock();
    stop.request_stop();
    state_->changed.notify_all();
}
std::vector<AssetJobInfo> AssetBuildQueue::snapshot() const {
    std::lock_guard lock(state_->mutex);
    std::vector<AssetJobInfo> result;
    for (const auto& [id, job] : state_->jobs) {
        (void)id;
        result.push_back(job->info);
    }
    return result;
}
std::vector<AssetJobCompletion> AssetBuildQueue::drain() {
    std::lock_guard lock(state_->mutex);
    std::vector<AssetJobCompletion> result;
    for (const auto& [id, job] : state_->jobs) {
        (void)id;
        if (finished(job->info.state) && !job->executing && !job->delivered) {
            if (job->artifact)
                state_->retained_bytes -= job->artifact->byte_size();
            result.push_back({job->info, std::move(job->artifact)});
            job->delivered = true;
        }
    }
    return result;
}
bool AssetBuildQueue::wait_idle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(state_->mutex);
    return state_->changed.wait_for(lock, timeout, [&] {
        return std::all_of(state_->jobs.begin(), state_->jobs.end(), [](const auto& item) {
            return finished(item.second->info.state) && !item.second->executing;
        });
    });
}
} // namespace forge
