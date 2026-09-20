#include <atomic>
#include <condition_variable>
#include <forge/asset_jobs.hpp>
#include <future>
#include <iostream>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid job accepted");
}
const std::string key(64, 'a');
CachedArtifact result() { return {key, {}, {{"file", {std::byte{1}}}}}; }
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = false;
    std::promise<void> started;
    CachedArtifact run(std::stop_token stop, const AssetBuildQueue::Progress&) {
        started.set_value();
        std::stop_callback wake(stop, [&] { changed.notify_all(); });
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return open || stop.stop_requested(); });
        return result();
    }
    void release() {
        {
            std::lock_guard lock(mutex);
            open = true;
        }
        changed.notify_all();
    }
};
AssetJobInfo info(const std::vector<AssetJobCompletion>& results, AssetJobId id) {
    for (const auto& result : results)
        if (result.info.id == id)
            return result.info;
    throw std::runtime_error("Missing job completion");
}
} // namespace
int main() {
    try {
        {
            Gate gate;
            AssetBuildQueue queue(1, 8);
            const auto started = gate.started.get_future();
            const auto asset = AssetId::generate();
            auto task = [&](std::stop_token stop, const auto& progress) {
                return gate.run(stop, progress);
            };
            const auto original = queue.submit(asset, 1, key, 0, {}, task);
            require(started.wait_for(2s) == std::future_status::ready, "Worker did not start");
            require(queue.submit(asset, 1, key, 1, {}, task) == original,
                    "Identical load was not coalesced");
            const auto next = queue.submit(asset, 2, key, 0, {}, [](auto, const auto& progress) {
                progress(0.5, "Validate");
                return result();
            });
            require(queue.wait_idle(2s), "Generation cancellation failed to wake worker");
            auto completed = queue.drain();
            require(info(completed, original).state == AssetJobState::Stale &&
                        info(completed, next).state == AssetJobState::Ready,
                    "Late result adopted");
            require(!completed[0].artifact && completed[1].artifact, "Stale artifact retained");
            rejects([&] { queue.submit(asset, 1, key, 0, {}, task); });
            require(queue.drain().empty(), "Completions delivered twice");
        }
        {
            Gate gate;
            AssetBuildQueue queue(1, 8);
            const auto started = gate.started.get_future();
            const auto blocker = queue.submit(
                AssetId::generate(), 1, key, 0, {},
                [&](auto stop, const auto& progress) { return gate.run(stop, progress); });
            require(started.wait_for(2s) == std::future_status::ready,
                    "Priority fixture did not start");
            std::vector<int> order;
            const auto low =
                queue.submit(AssetId::generate(), 1, key, 0, {}, [&](auto, const auto&) {
                    order.push_back(1);
                    return result();
                });
            const auto high =
                queue.submit(AssetId::generate(), 1, key, 10, {}, [&](auto, const auto&) {
                    order.push_back(2);
                    return result();
                });
            const auto dependent =
                queue.submit(AssetId::generate(), 1, key, 100, {low, high}, [&](auto, const auto&) {
                    order.push_back(3);
                    return result();
                });
            gate.release();
            require(queue.wait_idle(2s), "Priority/dependency queue stalled");
            require(order == std::vector<int>{2, 1, 3}, "Priority/dependency execution order");
            auto completed = queue.drain();
            require(info(completed, blocker).state == AssetJobState::Ready &&
                        info(completed, dependent).state == AssetJobState::Ready,
                    "Dependency result missing");
            auto failure = queue.submit(AssetId::generate(), 1, key, 0, {},
                                        [](auto, const auto&) -> CachedArtifact {
                                            throw std::runtime_error("conversion rejected");
                                        });
            std::atomic<bool> ran = false;
            auto blocked =
                queue.submit(AssetId::generate(), 1, key, 0, {failure}, [&](auto, const auto&) {
                    ran = true;
                    return result();
                });
            require(queue.wait_idle(2s), "Dependency failure not propagated");
            completed = queue.drain();
            require(!ran && info(completed, blocked).state == AssetJobState::Failed &&
                        info(completed, blocked).diagnostic.find("conversion rejected") !=
                            std::string::npos,
                    "Dependent ran after prerequisite failed");
        }
        {
            Gate gate;
            AssetBuildQueue queue(1, 2, 1);
            const auto started = gate.started.get_future();
            auto running = queue.submit(
                AssetId::generate(), 1, key, 0, {},
                [&](auto stop, const auto& progress) { return gate.run(stop, progress); });
            require(started.wait_for(2s) == std::future_status::ready,
                    "Cancel fixture did not start");
            std::atomic<bool> ran = false;
            auto pending = queue.submit(AssetId::generate(), 1, key, 0, {}, [&](auto, const auto&) {
                ran = true;
                return result();
            });
            rejects([&] {
                queue.submit(AssetId::generate(), 1, key, 0, {},
                             [](auto, const auto&) { return result(); });
            });
            queue.cancel(pending);
            queue.cancel(running);
            require(queue.wait_idle(2s), "Cancelled worker did not finish");
            const auto completed = queue.drain();
            require(!ran && info(completed, pending).state == AssetJobState::Cancelled &&
                        info(completed, running).state == AssetJobState::Cancelled,
                    "Cancellation result resurrected");
            const auto large =
                queue.submit(AssetId::generate(), 1, key, 0, {}, [](auto, const auto&) {
                    auto output = result();
                    output.files[0].bytes.resize(2);
                    return output;
                });
            require(queue.wait_idle(2s) &&
                        info(queue.drain(), large).state == AssetJobState::Failed,
                    "Result byte budget ignored");
        }
        {
            Gate gate;
            const auto started = gate.started.get_future();
            auto queue = std::make_unique<AssetBuildQueue>(1, 1);
            queue->submit(AssetId::generate(), 1, key, 0, {}, [&](auto stop, const auto& progress) {
                return gate.run(stop, progress);
            });
            require(started.wait_for(2s) == std::future_status::ready,
                    "Shutdown fixture did not start");
            queue
                .reset(); // Destruction cancels and joins; no detached callback into unloaded code.
        }
        {
            AssetBuildQueue::Progress late;
            auto queue = std::make_unique<AssetBuildQueue>(1, 4);
            const auto failure = queue->submit(
                AssetId::generate(), 1, key, 0, {},
                [](auto, const auto&) -> CachedArtifact { throw std::runtime_error(""); });
            const auto success =
                queue->submit(AssetId::generate(), 1, key, 0, {}, [&](auto, const auto& progress) {
                    late = progress;
                    return result();
                });
            require(queue->wait_idle(2s), "Empty-error fixture stalled");
            const auto completed = queue->drain();
            require(info(completed, failure).state == AssetJobState::Failed &&
                        !info(completed, failure).diagnostic.empty() &&
                        info(completed, success).state == AssetJobState::Ready,
                    "Empty exception became success");
            late(0.5, "Late progress");
            queue.reset();
            late(1, "Owner gone");
        }
        {
            Gate gate;
            AssetBuildQueue queue(2, 8);
            const auto parent_asset = AssetId::generate();
            const auto parent = queue.submit(parent_asset, 1, key, 0, {},
                                             [](auto, const auto&) { return result(); });
            require(queue.wait_idle(2s), "Prerequisite fixture did not finish");
            (void)queue.drain();
            const auto started = gate.started.get_future();
            const auto child = queue.submit(
                AssetId::generate(), 1, key, 0, {parent},
                [&](auto stop, const auto& progress) { return gate.run(stop, progress); });
            require(started.wait_for(2s) == std::future_status::ready,
                    "Dependent fixture did not start");
            queue.submit(parent_asset, 2, key, 0, {}, [](auto, const auto&) { return result(); });
            gate.release();
            require(queue.wait_idle(2s), "Superseded prerequisite stalled queue");
            const auto completed = queue.drain();
            require(info(completed, child).state == AssetJobState::Stale,
                    "Dependent adopted superseded prerequisite output");
            for (const auto& completion : completed)
                if (completion.info.id == child)
                    require(!completion.artifact, "Stale dependent retained output");
        }
        std::cout << "Asset queue priority, coalescing, dependencies, cancellation, generations, "
                     "budgets and shutdown passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
