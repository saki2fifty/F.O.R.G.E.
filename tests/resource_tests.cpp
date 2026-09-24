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
        auto data =
            std::make_unique<MeshResourceData>(MeshResourceData{mesh(x), {{0, "default", {}}}});
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
        require(pool.revisions().size() == 1 && pool.revisions()[0].strong_leases == 1 &&
                    pool.pending_requests().empty(),
                "Loaded resource inspection did not count its strong lease");
        auto weak = first.weak();
        const auto original = first.identity();
        require(first && first->mesh.lods[0].parts[0].bounds.maximum[0] == 1,
                "Ready resource absent");
        require(pool.evict_idle(0) == 0, "Evicted strong lease");
        auto newer = pool.request(asset, revision('b'), 2, load(2));
        require(pool.wait(newer, 5s), "Replacement failed");
        require(first->mesh.lods[0].parts[0].bounds.maximum[0] == 1 &&
                    pool.current(asset)->mesh.lods[0].parts[0].bounds.maximum[0] == 2,
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
        require(pool.current(asset)->mesh.lods[0].parts[0].bounds.maximum[0] == 2,
                "Failed reload lost last good");
        require(pool.pending_requests().size() == 1 &&
                    pool.pending_requests()[0].state == ResourceState::Failed &&
                    pool.pending_requests()[0].previous_good,
                "Resource inspection lost failed replacement/previous-good context");
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
        require(leased->mesh.lods[0].parts[0].bounds.maximum[0] == 2,
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
                    pool.current(child)->mesh.lods[0].parts[0].bounds.maximum[0] == 1,
                "Cancel discarded last good");
        // Compatibility failure happens at owner boundary, before selection.
        Pool compatible({}, [](const MeshResourceData& candidate, const MeshResourceData* prior) {
            if (prior && candidate.mesh.lods[0].parts[0].bounds.maximum[0] !=
                             prior->mesh.lods[0].parts[0].bounds.maximum[0])
                throw std::runtime_error("binding mismatch");
        });
        auto initial = compatible.request(asset, revision('a'), 1, load());
        require(compatible.wait(initial, 5s), "Initial compatible load failed");
        auto incompatible = compatible.request(asset, revision('b'), 2, load(2));
        require(!compatible.wait(incompatible, 5s) &&
                    compatible.current(asset)->mesh.lods[0].parts[0].bounds.maximum[0] == 1,
                "Incompatible replacement adopted");
        // Completed/live/retired bytes share one budget; strong references pin it.
        ResourcePoolLimits limits;
        limits.bytes = load()(std::stop_token{}).value->resident_bytes();
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
        // A failed replacement must not make an unleased last-good revision
        // permanently immune to explicit idle eviction under memory pressure.
        {
            Pool retries(limits);
            auto good = retries.request(asset, revision('a'), 1, load());
            require(retries.wait(good, 5s), "Eviction recovery fixture did not load");
            auto held = retries.current(asset);
            auto bad = retries.request(asset, revision('b'), 2,
                                       [](std::stop_token) -> ResourceCandidate<MeshAsset> {
                                           throw std::runtime_error("invalid replacement");
                                       });
            const auto failure_deadline = std::chrono::steady_clock::now() + 5s;
            while (!resource_detail::terminal(bad.inspect().state)) {
                require(std::chrono::steady_clock::now() < failure_deadline,
                        "Replacement failure never became observable");
                std::this_thread::yield();
            }
            // Deliberately observe worker failure before any owner-thread pump.
            require(bad.inspect().state == ResourceState::Failed && bad.inspect().previous_good,
                    "Failed update lost its last-good resource");
            require(retries.evict_idle(0) == 0, "Failed update allowed eviction of a live lease");
            held = {};
            require(retries.evict_idle(0) == 1 && retries.statistics().memory.total() == 0,
                    "Failed replacement pinned idle resource memory indefinitely");
            require(bad.inspect().state == ResourceState::Failed &&
                        bad.inspect().diagnostic == "invalid replacement" &&
                        !bad.inspect().previous_good && !retries.current(asset),
                    "Eviction lost the failure or claimed a nonexistent last-good revision");
            auto other = retries.request(child, revision('c'), 1, load());
            require(retries.wait(other, 5s), "Eviction did not recover the resource budget");
            require(retries.evict_idle(0) == 1, "Recovery resource was not idle");
            auto retry = retries.request(asset, revision('b'), 2, load(2));
            require(retries.wait(retry, 5s), "Retry of the failed source generation failed");
        }
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
        // One logical asset may have simultaneous semantic/backend variants.
        {
            Pool variants;
            auto color = variants.request(asset, revision('a'), 1, load(1), {}, 0, "d3d12.srgb");
            auto linear = variants.request(asset, revision('b'), 1, load(2), {}, 0, "d3d12.linear");
            require(variants.wait(color, 5s) && variants.wait(linear, 5s),
                    "Variants displaced each other");
            auto old_color = variants.acquire(color);
            require(variants.acquire(linear)->mesh.lods[0].parts[0].bounds.maximum[0] == 2 &&
                        old_color.identity().asset == asset.id &&
                        old_color.identity().variant == "d3d12.srgb" && !variants.current(asset),
                    "Variant identity/default lookup incorrect");
            auto changed = variants.request(asset, revision('c'), 2, load(3), {}, 0, "d3d12.srgb");
            require(variants.wait(changed, 5s) && linear.inspect().state == ResourceState::Ready &&
                        old_color->mesh.lods[0].parts[0].bounds.maximum[0] == 1,
                    "Variant reload crossed selection/lifetime");
            rejects(
                [&] { variants.request(asset, revision('d'), 1, load(), {}, 0, "d3d12.srgb"); });
            rejects([&] { variants.request(asset, revision('a'), 1, load(), {}, 0, "bad/key"); });
            variants.unload(asset, "d3d12.srgb");
            variants.collect();
            require(!variants.current(asset, "d3d12.srgb") &&
                        variants.current(asset, "d3d12.linear") &&
                        variants.statistics().selected == 1,
                    "Variant unload crossed selection");
        }
        // Sparse bindings cover used slots in every LOD, never all source slots.
        auto bound_mesh = MeshResourceData{mesh(), {{0, "default", {}}}};
        bound_mesh.mesh.material_slots = 65536;
        auto second_lod = bound_mesh.mesh.lods[0];
        second_lod.screen_coverage = .5f;
        second_lod.parts[0].material_slot = 65000;
        bound_mesh.mesh.lods.push_back(second_lod);
        const AssetRef<MaterialAsset> source_material{AssetId::generate()},
            replacement_material{AssetId::generate()};
        bound_mesh.materials.push_back({65000, "surface:paint", source_material});
        validate_mesh_material_bindings(bound_mesh);
        std::vector<MaterialSlotOverride> overrides{{"surface:paint", replacement_material},
                                                    {"removed", source_material}};
        const auto resolved = select_mesh_materials(bound_mesh, overrides);
        require(resolved.bindings.size() == 2 &&
                    resolved.bindings[1].material == replacement_material &&
                    resolved.unresolved == std::vector<std::string>{"removed"} &&
                    overrides.size() == 2,
                "Sparse/removed material override retargeted or discarded");
        overrides[0].material = {};
        require(!select_mesh_materials(bound_mesh, overrides).bindings[1].material.id,
                "Explicit default assignment ignored");
        overrides.clear();
        require(select_mesh_materials(bound_mesh, overrides).bindings[1].material ==
                    source_material,
                "Absent override did not follow mesh material");
        overrides = {{"surface:paint", {}}, {"surface:paint", replacement_material}};
        rejects([&] { select_mesh_materials(bound_mesh, overrides); });
        auto invalid_binding = bound_mesh;
        invalid_binding.materials.pop_back();
        rejects([&] { validate_mesh_material_bindings(invalid_binding); });
        invalid_binding = bound_mesh;
        invalid_binding.materials[1].key = "default";
        rejects([&] { validate_mesh_material_bindings(invalid_binding); });
        invalid_binding = bound_mesh;
        invalid_binding.materials[1].physical_slot = 1;
        rejects([&] { validate_mesh_material_bindings(invalid_binding); });
        for (const auto& key : {std::string{}, std::string(256, 'a'), std::string("bad/key")}) {
            overrides = {{key, {}}};
            rejects([&] { select_mesh_materials(bound_mesh, overrides); });
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
        require(actual.wait(actual_ticket, 5s) && actual.current(asset)->mesh.byte_size() == 48,
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
