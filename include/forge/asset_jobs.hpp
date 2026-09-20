#pragma once
#include <chrono>
#include <forge/derived_cache.hpp>
#include <memory>
#include <stop_token>

namespace forge {
using AssetJobId = std::uint64_t;
enum class AssetJobState { Queued, Running, Waiting, Ready, Failed, Cancelled, Stale };
struct AssetJobInfo {
    AssetJobId id = 0;
    AssetId asset;
    std::uint64_t generation = 0;
    std::string build_key;
    AssetJobState state = AssetJobState::Queued;
    int priority = 0;
    double progress = 0;
    std::string stage;
    std::string diagnostic;
    std::vector<AssetJobId> dependencies;
};
struct AssetJobCompletion {
    AssetJobInfo info;
    std::shared_ptr<const CachedArtifact> artifact;
};
// A bounded asset build queue, not a world/device executor. Owner drains copied
// completions and separately validates current source/catalog before publication.
class AssetBuildQueue {
  public:
    using Progress = std::function<void(double, std::string)>;
    using Task = std::function<CachedArtifact(std::stop_token, const Progress&)>;
    explicit AssetBuildQueue(unsigned workers = 2, std::size_t capacity = 64,
                             std::size_t completed_byte_budget = 512 * 1024 * 1024);
    ~AssetBuildQueue();
    AssetBuildQueue(const AssetBuildQueue&) = delete;
    AssetBuildQueue& operator=(const AssetBuildQueue&) = delete;
    AssetJobId submit(AssetId asset, std::uint64_t generation, std::string build_key, int priority,
                      std::vector<AssetJobId> dependencies, Task task);
    void cancel(AssetJobId id);
    std::vector<AssetJobInfo> snapshot() const;
    std::vector<AssetJobCompletion> drain();
    bool wait_idle(std::chrono::milliseconds timeout);

  private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace forge
