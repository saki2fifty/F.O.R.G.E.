// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "collision_resource.hpp"
#include "collision_shape.hpp"
#include "cooked_envelope.hpp"
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <forge/collision_asset.hpp>
#include <forge/collision_generation.hpp>
#include <forge/collision_source.hpp>
#include <future>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid collision accepted");
}
CollisionNode node(CollisionKind kind) {
    CollisionNode n;
    n.id = CollisionMemberId::generate();
    n.kind = kind;
    n.dimensions = {};
    if (kind == CollisionKind::Box)
        n.dimensions = {2, 2, 2};
    if (kind == CollisionKind::Sphere)
        n.dimensions = {1, 0, 0};
    if (kind == CollisionKind::Capsule || kind == CollisionKind::Cylinder)
        n.dimensions = {1, 2, 0};
    if (kind == CollisionKind::ConvexHull)
        n.vertices = {{0, 0, 0}, {6, 0, 0}, {0, 2, 0}, {0, 0, 2}};
    if (kind == CollisionKind::TriangleMesh) {
        n.vertices = {{-2, 0, -2}, {0, 0, 2}, {2, 0, -2}};
        n.indices = {0, 1, 2};
    }
    return n;
}
template <class F> auto edit_metadata(const std::vector<std::byte>& b, F f) {
    std::array<std::byte, 8> magic;
    std::copy_n(b.begin(), 8, magic.begin());
    auto envelope = asset_detail::decode_envelope(b, magic, 1024 * 1024, 64 * 1024 * 1024);
    f(envelope.metadata);
    return asset_detail::encode_envelope(envelope.metadata, envelope.payload, magic, 1024 * 1024);
}
} // namespace
int main() {
    try {
        for (auto kind :
             {CollisionKind::Box, CollisionKind::Sphere, CollisionKind::Capsule,
              CollisionKind::Cylinder, CollisionKind::ConvexHull, CollisionKind::TriangleMesh}) {
            CollisionData d{{node(kind)}};
            auto bytes = encode_collision(d);
            auto decoded = decode_collision(bytes);
            require(encode_collision(decoded) == bytes, "Collision round trip drift");
            auto native = physics_detail::prepare_collision(decoded);
            require(bool(native->shape), "No native collision shape");
            require(native->static_only == (kind == CollisionKind::TriangleMesh),
                    "Incorrect static complex classification");
            for (std::size_t cut = 0; cut < bytes.size(); ++cut)
                rejects([&] { decode_collision(std::span(bytes).first(cut)); });
            auto corrupt =
                edit_metadata(bytes, [](auto& j) { j["nodes"][0]["vertices"] = UINT32_MAX; });
            rejects([&] { decode_collision(corrupt); });
            corrupt = edit_metadata(bytes, [](auto& j) { j["nodes"][0]["indices"] = -1; });
            rejects([&] { decode_collision(corrupt); });
            corrupt = edit_metadata(bytes, [](auto& j) { j["root"] = 0.5; });
            rejects([&] { decode_collision(corrupt); });
            corrupt = edit_metadata(bytes, [](auto& j) { j["nodes"][0]["kind"] = 99; });
            rejects([&] { decode_collision(corrupt); });
            d.nodes[0].scale = {-1, 1, 1};
            require(bool(physics_detail::prepare_collision(d)->shape), "Signed scale rejected");
            d.nodes[0].scale = {1, 2, 1};
            if (kind == CollisionKind::Sphere || kind == CollisionKind::Capsule)
                rejects([&] { physics_detail::prepare_collision(d); });
            else
                require(bool(physics_detail::prepare_collision(d)->shape),
                        "Supported scale rejected");
            d.nodes[0].scale = {1, 1, 2};
            if (kind == CollisionKind::Cylinder)
                rejects([&] { physics_detail::prepare_collision(d); });
            d.nodes[0].scale = {0, 1, 1};
            rejects([&] { physics_detail::prepare_collision(d); });
            d.nodes[0].scale = {1e-20f, 1, 1};
            rejects([&] { physics_detail::prepare_collision(d); });
        }
        CollisionData d{{node(CollisionKind::TriangleMesh)}};
        d.nodes[0].indices[2] = 30;
        rejects([&] { encode_collision(d); });
        d.nodes[0].indices = {0, 0, 0};
        rejects([&] { physics_detail::prepare_collision(d); });
        d.nodes[0] = node(CollisionKind::ConvexHull);
        d.nodes[0].vertices[0][0] = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { physics_detail::prepare_collision(d); });
        d.nodes[0].vertices.assign(4, {0, 0, 0});
        rejects([&] { physics_detail::prepare_collision(d); });
        d = {
            {node(CollisionKind::Compound), node(CollisionKind::Box), node(CollisionKind::Sphere)}};
        d.nodes[0].children = {1, 2};
        d.nodes[1].translation = {4, 0, 0};
        auto compound = physics_detail::prepare_collision(decode_collision(encode_collision(d)));
        const auto canonical_compound = encode_collision(d);
        JPH::RayCast ray(JPH::Vec3(4, 5, 0) - compound->shape->GetCenterOfMass(), {0, -10, 0});
        JPH::RayCastResult hit;
        require(compound->shape->CastRay(ray, {}, hit) && std::abs(hit.mFraction - .4f) < .001f,
                "Compound child origin/COM placement is wrong");
        const auto token = compound->shape->GetSubShapeUserData(hit.mSubShapeID2);
        require(token > 0 && token <= compound->members.size() &&
                    compound->members[token - 1] == d.nodes[1].id,
                "Compound hit lost stable leaf member identity");
        d.nodes[0].children = {2, 1};
        require(encode_collision(d) == canonical_compound,
                "Compound child order changed cooked bytes");
        auto reordered = physics_detail::prepare_collision(d);
        require(reordered->members == compound->members, "Reorder changed member mapping");
        {
            auto scaled = d;
            scaled.nodes[0].scale = {-2, 2, 2};
            require(bool(physics_detail::prepare_collision(scaled)->shape),
                    "Uniform-magnitude reflected compound rejected");
            scaled.nodes[0].scale = {2, 1, 1};
            rejects([&] { physics_detail::prepare_collision(scaled); }); // Sphere child.
            scaled.nodes[2] = node(CollisionKind::Box);
            scaled.nodes[1].rotation = {0, 0, .38268343f, .92387953f};
            rejects(
                [&] { physics_detail::prepare_collision(scaled); }); // Rotated nonuniform scale.
            scaled.nodes[0].scale = {2, 2, 2};
            require(bool(physics_detail::prepare_collision(scaled)->shape),
                    "Representable rotated compound rejected");
        }
        d.nodes[0].children = {0, 2};
        rejects([&] { encode_collision(d); });
        d.nodes[0].children = {1, 1};
        rejects([&] { encode_collision(d); });
        d.nodes[0].children = {1};
        rejects([&] { encode_collision(d); });
        d.nodes[0].children = {1, 2};
        d.nodes[2].id = d.nodes[1].id;
        rejects([&] { encode_collision(d); });
        MeshPart part;
        part.vertices = 4;
        part.streams = {
            {"POSITION", 3, std::vector<float>{-1, 0, -1, 0, 0, 1, 1, 0, -1, 900, 900, 900}}};
        part.indices = {0, 1, 2};
        part.bounds = mesh_bounds(part);
        MeshData mesh{1, {{1, {part}}}};
        const auto member = CollisionMemberId::generate();
        auto generated = generate_collision(mesh, {0, {0}}, member);
        require(generated.data.nodes[0].vertices.size() == 3 &&
                    generated.data.nodes[0].id == member,
                "Generation included unused source geometry or changed member identity");
        require(bool(physics_detail::prepare_collision(generated.data)->shape),
                "Generated mesh failed native admission");
        rejects([&] { generate_collision(mesh, {0, {1}}, member); });
        rejects([&] { generate_collision(mesh, {0, {0, 0}}, member); });
        mesh.lods[0].parts[0].indices = {0, 1, 2, 0, 0, 0};
        rejects([&] { generate_collision(mesh, {0, {0}}, member); });
        generated = generate_collision(
            mesh, {0, {0}, CollisionKind::TriangleMesh, CollisionDegeneratePolicy::Remove}, member);
        require(generated.removed_triangles == 1 && generated.data.nodes[0].indices.size() == 3,
                "Explicit degenerate removal lost good triangles");
        auto source = CollisionSource::create(AssetId::generate(), CollisionKind::Box);
        source.document["extension"] = {{"opaque", "preserved"}};
        const auto source_text = source.document.dump();
        auto source_copy = CollisionSource::parse(std::as_bytes(std::span(source_text)));
        require(source_copy.document == source.document && source_copy.mesh_sources().empty(),
                "Authored collision round trip lost identity/extensions");
        require(
            bool(physics_detail::prepare_collision(resolve_collision_source(source_copy, {}).data)
                     ->shape),
            "Authored primitive failed preparation");
        const AssetRef<MeshAsset> source_mesh{AssetId::generate()};
        auto& recipe = source_copy.document["nodes"][0];
        recipe["kind"] = "triangle_mesh";
        recipe["dimensions"] = {0, 0, 0};
        recipe["source"] = {
            {"mesh", source_mesh}, {"lod", 0}, {"parts", {0}}, {"degenerate", "remove"}};
        recipe["source"]["revision"] = std::string(64, 'a');
        source_copy.validate();
        require(source_copy.mesh_sources() == std::vector{source_mesh},
                "Collision source omitted Mesh dependency");
        auto resolved = resolve_collision_source(source_copy, [&](AssetRef<MeshAsset> requested) {
            require(requested == source_mesh, "Resolved wrong Mesh identity");
            return mesh;
        });
        require(resolved.removed_triangles.size() == 1 &&
                    bool(physics_detail::prepare_collision(resolved.data)->shape),
                "Authored collision generation failed");
        recipe["source"]["parts"] = {0, 0};
        rejects([&] { source_copy.validate(); });
        recipe["source"]["parts"] = {0};
        recipe["children"] = {source_copy.document["root"]};
        rejects([&] { source_copy.validate(); });
        compound.reset();
        reordered.reset();
        // Concurrent prepare/retire exercises shared registration lifetime when
        // no PhysicsRuntime owns the process registration (asset workers).
        std::vector<std::future<void>> jobs;
        for (int worker = 0; worker < 8; ++worker)
            jobs.push_back(std::async(std::launch::async, [] {
                for (int i = 0; i < 50; ++i) {
                    auto prepared = physics_detail::prepare_collision({{node(CollisionKind::Box)}});
                    require(bool(prepared->shape), "Worker preparation failed");
                }
            }));
        for (auto& job : jobs)
            job.get();
        ResourcePool<CollisionAsset> pool({2, 16, 16, 16 * 1024 * 1024});
        const AssetRef<CollisionAsset> asset{AssetId::generate()};
        const auto good = encode_collision({{node(CollisionKind::Box)}});
        auto loader = [good](std::stop_token stop) {
            return prepare_collision_resource(good, stop);
        };
        auto drain = [&] {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (pool.statistics().pending) {
                pool.pump();
                require(std::chrono::steady_clock::now() < end,
                        "Collision resource load timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        auto first = pool.request(asset, std::string(64, 'a'), 1, loader);
        auto duplicate = pool.request(asset, std::string(64, 'a'), 1, loader);
        drain();
        auto lease = pool.acquire(first);
        require(bool(lease) && pool.acquire(duplicate)->native == lease->native,
                "Collision revision did not share immutable native shape");
        auto invalid = pool.request(asset, std::string(64, 'b'), 2, [](std::stop_token stop) {
            return prepare_collision_resource(std::span<const std::byte>{}, stop);
        });
        drain();
        require(invalid.inspect().state == ResourceState::Failed &&
                    pool.current(asset).identity() == lease.identity(),
                "Failed collision replacement discarded the previous shape");
        auto next = pool.request(asset, std::string(64, 'c'), 3, loader);
        drain();
        require(bool(pool.acquire(next)) && lease->native != pool.acquire(next)->native,
                "Collision replacement failed to retain distinct leased revisions");
        pool.unload(asset);
        require(!pool.current(asset) && bool(lease->native),
                "Unload invalidated a pinned collision shape");
        pool.close();
        require(!lease, "Closed collision resource scope remained usable");
        rejects([&] { lease.get(); });
        std::cout << "Collision admission/native shape tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
