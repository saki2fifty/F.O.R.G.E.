#include "asset_bytes.hpp"
#include <atomic>
#include <forge/mesh_resource.hpp>
#include <fstream>
#include <future>
#include <iostream>
#include <semaphore>
namespace forge {
struct LifetimeAsset {
    static constexpr const char* type = "fixture.lifetime";
};
struct LifetimeData {
    std::function<void()> destroyed;
    ~LifetimeData() {
        if (destroyed)
            destroyed();
    }
};
template <> struct ResourceTraits<LifetimeAsset> {
    using Data = LifetimeData;
};
} // namespace forge
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid resource operation accepted");
}
MeshData mesh(float x = 1) {
    MeshPart part;
    part.vertices = 3;
    part.streams = {{"POSITION", 3, std::vector<float>{0, 0, 0, x, 0, 0, 0, 1, 0}}};
    part.indices = {0, 1, 2};
    part.bounds = mesh_bounds(part);
    return {1, {{1, {part}}}, {}, {}};
}
using Pool = ResourcePool<MeshAsset>;
std::string revision(char c) { return std::string(64, c); }
Pool::Loader load(float x = 1) {
    return [x](std::stop_token) {
        auto data = std::make_unique<MeshData>(mesh(x));
        const auto n = data->resident_bytes();
        return ResourceCandidate<MeshAsset>{std::move(data), {n}};
    };
}
void drained(Pool& pool) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (pool.statistics().pending) {
        pool.pump();
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error("Resource queue did not drain");
        std::this_thread::sleep_for(1ms);
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need resource scratch path");
        const std::filesystem::path root = argv[1];
        std::filesystem::create_directories(root);
        Pool pool;
        const AssetRef<MeshAsset> asset{AssetId::generate()};
        std::atomic<unsigned> calls = 0;
        auto loader = [&](std::stop_token stop) {
            ++calls;
            return load()(stop);
        };
        std::vector<std::future<ResourceTicket>> requests;
        for (unsigned i = 0; i < 16; ++i)
            requests.push_back(std::async(
                std::launch::async, [&] { return pool.request(asset, revision('a'), 1, loader); }));
        std::vector<ResourceTicket> tickets;
        for (auto& r : requests)
            tickets.push_back(r.get());
        for (const auto& t : tickets)
            require(t.inspect().identity == tickets[0].inspect().identity,
                    "Concurrent requests did not coalesce");
        require(pool.wait(tickets[0], 5s) && calls == 1, "Coalesced loader ran more than once");
        auto first = pool.acquire(tickets[0]);
        auto weak = first.weak();
        const auto original = first.identity();
        require(first && first->lods[0].parts[0].bounds.maximum[0] == 1, "Ready resource absent");
        require(pool.evict_idle(0) == 0, "Evicted strong lease");
        auto newer = pool.request(asset, revision('b'), 2, load(2));
        require(pool.wait(newer, 5s), "Replacement failed");
        require(first->lods[0].parts[0].bounds.maximum[0] == 1 &&
                    pool.current(asset)->lods[0].parts[0].bounds.maximum[0] == 2,
                "Replacement mutated previous lease");
        require(pool.statistics().retired == 1 && weak.lock().identity() == original,
                "Old revision was not retained");
        first = {};
        pool.collect();
        require(!weak.lock() && pool.statistics().retired == 0,
                "Released old revision was not retired");
        rejects([&] { pool.request(asset, revision('a'), 1, load()); });
        rejects([&] { pool.request(asset, revision('c'), 2, load()); });
        auto failed = pool.request(asset, revision('c'), 3,
                                   [](std::stop_token) -> ResourceCandidate<MeshAsset> {
                                       throw std::runtime_error("decode rejected");
                                   });
        require(!pool.wait(failed, 5s) && failed.inspect().state == ResourceState::Failed &&
                    failed.inspect().previous_good,
                "Failure lost diagnostic/previous-good state");
        require(pool.current(asset)->lods[0].parts[0].bounds.maximum[0] == 2,
                "Failed reload lost last good");
        const AssetRef<MeshAsset> missing{AssetId::generate()};
        auto resolution = pool.resolve(missing, pool.current(asset));
        require(resolution.fallback && resolution.lease &&
                    resolution.requested.state == ResourceState::Unloaded,
                "Fallback pretended missing resource loaded");
        resolution = {};
        // Wrong-thread access is separate from supported concurrent request/inspection.
        auto leased = pool.current(asset);
        std::async(std::launch::async, [&] { rejects([&] { leased.get(); }); }).get();
        std::async(std::launch::async, [&] { rejects([&] { leased.weak().lock(); }); }).get();
        Pool other;
        rejects([&] { other.acquire(newer); });
        // Late completion after unload cannot resurrect a selected resource.
        std::binary_semaphore entered{0}, release{0};
        auto late = pool.request(asset, revision('d'), 4, [&](std::stop_token stop) {
            entered.release();
            require(release.try_acquire_for(5s), "Loader release timed out");
            return load(3)(stop);
        });
        require(entered.try_acquire_for(5s), "Loader never started");
        pool.unload(asset);
        release.release();
        drained(pool);
        require(!pool.current(asset) && late.inspect().state == ResourceState::Stale,
                "Unload resurrected resource");
        require(leased->lods[0].parts[0].bounds.maximum[0] == 2,
                "Unload invalidated active old lease");
        leased = {};
        pool.collect();
        require(pool.statistics().memory.total() == 0, "Retired memory remained accounted");
        auto retry = pool.request(asset, revision('e'), 5, load());
        require(pool.wait(retry, 5s), "Unload/reload failed");
        require(retry.inspect().identity != original && !pool.acquire(tickets[0]),
                "Stale request retargeted reused slot");
        // Dependencies wait for owner adoption and report their actual failure chain.
        const AssetRef<MeshAsset> child{AssetId::generate()}, bad{AssetId::generate()};
        auto broken = pool.request(bad, revision('f'), 1,
                                   [](std::stop_token) -> ResourceCandidate<MeshAsset> {
                                       throw std::runtime_error("missing dependency");
                                   });
        auto dependent = pool.request(child, revision('a'), 1, load(), {broken});
        require(!pool.wait(dependent, 5s) &&
                    dependent.inspect().state == ResourceState::DependencyFailed &&
                    dependent.inspect().diagnostic.find(bad.id.str()) != std::string::npos,
                "Dependency failure was hidden");
        auto good = pool.request(child, revision('b'), 2, load(), {retry});
        require(pool.wait(good, 5s), "Ready dependency did not load");
        // A dependency can disappear after loading started but before adoption.
        std::binary_semaphore dep_entered{0}, dep_release{0};
        auto dep_changed = pool.request(child, revision('d'), 3,
                                        [&](std::stop_token stop) {
                                            dep_entered.release();
                                            require(dep_release.try_acquire_for(5s),
                                                    "Dependency loader release timed out");
                                            return load(4)(stop);
                                        },
                                        {retry});
        require(dep_entered.try_acquire_for(5s), "Dependency loader did not start");
        pool.unload(asset);
        dep_release.release();
        require(!pool.wait(dep_changed, 5s) &&
                    dep_changed.inspect().state == ResourceState::DependencyFailed &&
                    pool.current(child),
                "Changed dependency candidate replaced last-good resource");
        // Cancelled coalesced requests retain the previous selected revision.
        auto cancelled = pool.request(child, revision('c'), 4, load(4));
        pool.cancel(child);
        drained(pool);
        require(cancelled.inspect().state == ResourceState::Cancelled &&
                    pool.current(child)->lods[0].parts[0].bounds.maximum[0] == 1,
                "Cancel discarded last good");
        // Compatibility failure happens at owner boundary, before selection.
        Pool compatible({}, [](const MeshData& candidate, const MeshData* prior) {
            if (prior && candidate.lods[0].parts[0].bounds.maximum[0] !=
                             prior->lods[0].parts[0].bounds.maximum[0])
                throw std::runtime_error("binding mismatch");
        });
        auto initial = compatible.request(asset, revision('a'), 1, load());
        require(compatible.wait(initial, 5s), "Initial compatible load failed");
        auto incompatible = compatible.request(asset, revision('b'), 2, load(2));
        require(!compatible.wait(incompatible, 5s) &&
                    compatible.current(asset)->lods[0].parts[0].bounds.maximum[0] == 1,
                "Incompatible replacement adopted");
        // Completed/live/retired bytes share one budget; strong references pin it.
        ResourcePoolLimits limits;
        limits.bytes = mesh().resident_bytes();
        Pool budget(limits);
        auto one = budget.request(asset, revision('a'), 1, load());
        require(budget.wait(one, 5s), "Budget fixture failed");
        auto pinned = budget.current(asset);
        auto two = budget.request(child, revision('b'), 1, load());
        require(!budget.wait(two, 5s) && budget.statistics().memory.total() == limits.bytes,
                "Budget exceeded");
        require(budget.evict_idle(0) == 0, "Budget evicted pinned resource");
        pinned = {};
        require(budget.evict_idle(0) == 1, "Idle LRU eviction failed");
        two = budget.request(child, revision('b'), 1, load());
        require(budget.wait(two, 5s), "Retry after eviction failed");
        // Rejected weak promotion cannot move physical destruction to another thread.
        {
            std::atomic<bool> destroyed = false, wrong_thread = false;
            const auto owner = std::this_thread::get_id();
            ResourcePool<LifetimeAsset> lifetime;
            const AssetRef<LifetimeAsset> key{AssetId::generate()};
            auto ticket = lifetime.request(key, revision('a'), 1, [&](std::stop_token) {
                auto data = std::make_unique<LifetimeData>();
                data->destroyed = [&] {
                    destroyed = true;
                    wrong_thread = std::this_thread::get_id() != owner;
                };
                return ResourceCandidate<LifetimeAsset>{std::move(data), {sizeof(LifetimeData)}};
            });
            require(lifetime.wait(ticket, 5s), "Lifetime fixture did not load");
            auto lease = lifetime.current(key);
            auto observation = lease.weak();
            lifetime.unload(key);
            require(!destroyed, "Unload destroyed pinned physical data");
            auto offender = std::async(std::launch::async, [&] {
                for (unsigned i = 0; i < 1000; ++i)
                    rejects([&] { observation.lock(); });
            });
            lease = {};
            lifetime.collect();
            offender.get();
            require(destroyed && !wrong_thread && ticket.inspect().state == ResourceState::Unloaded,
                    "Weak promotion changed owner destruction or retained unloaded readiness");
        }
        // Actual cooked-file provider validates content before runtime adoption.
        const auto bytes = encode_mesh(mesh());
        const auto digest = asset_detail::content_digest(bytes);
        const auto path = root / "mesh.fmesh";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        Pool actual;
        auto actual_ticket = actual.request(asset, digest, 1, mesh_resource_loader(path, digest));
        require(actual.wait(actual_ticket, 5s) && actual.current(asset)->byte_size() == 48,
                "Actual cooked mesh provider failed");
        auto corrupt =
            actual.request(asset, revision('a'), 2, mesh_resource_loader(path, revision('a')));
        require(!actual.wait(corrupt, 5s) && actual.current(asset),
                "Corrupt cooked file replaced good resource");
        auto survivor = actual.current(asset);
        auto observed = survivor.weak();
        actual.close();
        require(!survivor && !observed.lock(), "Closed owner scope remained live");
        rejects([&] { survivor.get(); });
        std::filesystem::remove(path);
        std::cout << "Typed mesh loads, concurrent coalescing, immutable leases, dependencies, "
                     "cancellation, stale completion, fallback, budgets and shutdown passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
