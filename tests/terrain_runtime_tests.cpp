#include "model_render_resource.hpp"
#include "navigation_geometry.hpp"
#include "runtime_package.hpp"
#include <forge/game_session.hpp>
#include <forge/terrain_source.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Missing terrain fixture: " + path.string());
    return Json::parse(in);
}
void exercise(const std::filesystem::path& root, const Json& authored,
              const TerrainRaster& oracle) {
    GameSessionConfig config;
    config.content_root = root;
    GameSession game(config);
    const auto ticket = game.prepare(authored);
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!game.poll_preparation(ticket).ready) {
        require(std::chrono::steady_clock::now() < deadline,
                "Terrain native collision preparation timed out");
        std::this_thread::sleep_for(1ms);
    }
    game.activate(ticket, RuntimeClock::Time{}, false);
    game.step();
    auto physics = game.active().physics();
    std::vector<EntityRef> falling;
    EntityRef ground;
    for (const auto& row : authored.at("entities")) {
        const auto& components = row.at("components");
        if (!components.contains("forge.physics_body"))
            continue;
        const auto ref = *game.active().engine.world().reference(
            game.active().scene.entity(row.at("id").get<std::string>()).id());
        if (components.contains("forge.asset_collider"))
            ground = ref;
        else
            falling.push_back(ref);
    }
    require(bool(ground.entity) && falling.size() == 3,
            "Gallery55 is missing terrain or falling bodies");
    // Probe both tile interiors, the shared seam, and the authored raised hill.
    for (const auto& point : std::array<std::array<double, 2>, 6>{
             {{4, 4}, {26, 4}, {16, 12}, {12, 8}, {16, 8}, {15.75, 7.25}}}) {
        const auto height = terrain_height(oracle, point[0], point[1]);
        const auto hit = physics->raycast({point[0] - 16, *height + 1, point[1] - 8}, {0, -2, 0});
        require(hit && hit->entity == ground && std::abs(hit->position[1] - *height) < 2e-4,
                "Native terrain triangles differ from the rendered float surface");
    }
    std::vector<PhysicsBodyState> first;
    for (const auto ref : falling) {
        const auto state = physics->body_state(ref);
        require(state && state->motion == 2 && state->linear_velocity[1] < 0,
                "Terrain Gallery falling body did not realize gravity");
        first.push_back(*state);
    }
    // Presentation and paused observation cannot advance or mutate physics.
    for (unsigned i = 0; i < 20; ++i) {
        (void)game.presentation();
        (void)physics->status();
    }
    for (unsigned i = 0; i < falling.size(); ++i)
        require(physics->body_state(falling[i]) == first[i],
                "Terrain paused observation advanced a body");
    std::array<bool, 3> contacted{};
    for (unsigned tick = 0; tick < 360; ++tick) {
        game.step();
        for (const auto& contact : physics->contacts())
            for (unsigned i = 0; i < falling.size(); ++i)
                if ((contact.first == ground && contact.second == falling[i]) ||
                    (contact.second == ground && contact.first == falling[i]))
                    contacted[i] = true;
    }
    for (unsigned i = 0; i < falling.size(); ++i) {
        const auto state = physics->body_state(falling[i]);
        require(state && contacted[i], "A falling sphere never contacted actual terrain");
        const auto& centre = state->centre_of_mass;
        const auto height = terrain_height(oracle, centre[0] + 16, centre[2] + 8);
        require(height && centre[1] - *height > .32 && centre[1] - *height < .43 &&
                    centre[1] < first[i].centre_of_mass[1] - 3,
                "Terrain sphere fell through the surface or failed to land at its scaled radius");
    }
    require(game.active().scene.snapshot().at("entities").size() == authored.at("entities").size(),
            "Terrain created per-sample scene entities");
    game.unload(RuntimeClock::Time{} + 10s);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery root and terrain scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto path = std::filesystem::path("Scenes/55-layered-heightfield-terrain.scene.json");
        const auto scene = read(root / path);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const AssetRecord* terrain = nullptr;
        for (const auto& [id, record] : catalog->records())
            if (record.metadata.contains("forge.terrain")) {
                require(!terrain, "Unexpected additional Gallery terrain");
                terrain = &record;
            }
        require(terrain, "Gallery terrain root Mesh record missing");
        const TerrainSource source{read(root / terrain->source)};
        const auto oracle = source.raster();
        require(oracle.tiles.size() == 2 && oracle.layers.size() == 2,
                "Gallery terrain lacks adjoining layered tiles");
        ResourcePool<MeshAsset> pool;
        const auto mesh = asset_detail::request_model_mesh(pool, root, catalog, {source.asset()});
        require(pool.wait(mesh, 15s), "Gallery terrain rendered Mesh did not load");
        const auto lease = pool.acquire(mesh);
        require(bool(lease) && encode_mesh(lease->mesh) == encode_mesh(source.mesh()),
                "Gallery render resource differs from authored terrain triangles");
        // The existing navigation geometry owner consumes this same root Mesh.
        // Mark only the ground in a detached test Scene; saved Gallery bytes stay intact.
        EngineContext authoring;
        Scene nav_scene(authoring.world());
        nav_scene.restore_snapshot(scene);
        for (const auto& row : scene.at("entities"))
            if (row.at("components").contains("forge.asset_collider"))
                nav_scene.entity(row.at("id").get<std::string>()).set<NavigationSurface>({true});
        const auto nav_sources = navigation_detail::capture(nav_scene.effective_document());
        const auto nav_inputs = navigation_detail::mesh_inputs(nav_sources, [&](AssetId id) {
            return asset_detail::selected_mesh_revision(*catalog, {id});
        });
        const auto nav_geometry = navigation_detail::geometry(
            nav_sources, nav_inputs, [&](AssetId id) -> const MeshResourceData& {
                require(id == source.asset(), "Terrain navigation resolved an unrelated Mesh");
                return lease.get();
            });
        std::vector<float> expected_triangles;
        for (const auto& part : lease->mesh.lods[0].parts) {
            const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
            for (const auto index : part.indices) {
                expected_triangles.push_back(positions[index * 3] - 16);
                expected_triangles.push_back(positions[index * 3 + 1]);
                expected_triangles.push_back(positions[index * 3 + 2] - 8);
            }
        }
        require(nav_inputs.size() == 1 && nav_geometry.vertices == expected_triangles &&
                    nav_geometry.indices.size() == expected_triangles.size() / 3,
                "Navigation did not consume the exact placed terrain triangles");
        require(!navigation_detail::build_tile(nav_geometry, {}).empty(),
                "Existing Recast builder did not produce terrain walkable polygons");
        exercise(root, scene, oracle);
        require(read(root / path) == scene && read(root / terrain->source) == source.document,
                "Terrain Play changed authored files");
        const std::array roots{scene.at("asset_id").get<AssetId>()};
        {
            auto owner = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*owner, roots, {});
        }
        const auto exported = std::filesystem::path(root.string() + "-export");
        (void)package_runtime_content(root, exported, roots, {"windows", "d3d12"});
        const auto relocated = std::filesystem::path(exported.string() + "-relocated");
        std::filesystem::rename(exported, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        require(shipped.records().size() == 3 &&
                    !std::filesystem::exists(relocated / "Assets/Terrain"),
                "Terrain export included unrelated records or authoring grids");
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        exercise(relocated, read(relocated / shipped.records().at(roots[0]).source), oracle);
        std::cout << "Gallery55 shared render/collision triangles, native falling bodies, paused "
                     "isolation and relocated source-offline export passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
