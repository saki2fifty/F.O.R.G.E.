#include "asset_bytes.hpp"
#include "fbx_fixture.hpp"
#include "import_authoring.hpp"
#include "model_instance_pose.hpp"
#include "model_placement.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include "texture_encode.h"
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
namespace {
constexpr auto scene_path = "Scenes/175-fbx-model-import.scene.json";
constexpr auto source_folder = "Assets/FBX";
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void author(const std::filesystem::path& root, const std::filesystem::path& worker,
            const std::filesystem::path& converter) {
    check(!std::filesystem::exists(root / scene_path), "FBX Gallery already exists");
    const auto before = AssetCatalog::open_project(root).document().at("assets");
    std::filesystem::create_directories(root / source_folder);
    const std::array<std::uint8_t, 16> pixels{255, 74,  24,  255, 24, 190, 255, 255,
                                              255, 255, 255, 255, 40, 48,  90,  255};
    ForgeImageBytes png{};
    check(forge_png_encode_rgba8(pixels.data(), pixels.size(), 2, 2, 8, 1024 * 1024, &png),
          "Cannot encode original FBX checker");
    const std::string encoded(reinterpret_cast<const char*>(png.bytes), png.size);
    forge_image_bytes_free(&png);
    atomic_write(root / source_folder / "Checker.png", encoded);
    const auto lease = std::make_shared<ProjectLease>(root);
    const auto registry = asset_import_registry(worker, converter);
    std::map<std::string, AssetId> models;
    std::size_t added_records = 1;
    for (const auto& [name, source] :
         {std::pair{"Static.fbx", test::fbx_textured_triangle("Checker.png")},
          std::pair{"Moving.fbx", test::fbx_animated_triangle()},
          std::pair{"Skin.fbx", test::fbx_animated_triangle(true)}}) {
        const auto path = std::filesystem::path(source_folder) / name;
        if (std::filesystem::exists(root / path)) {
            const auto bytes = read_bytes(root / path, 1024 * 1024);
            check(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == source,
                  "Existing FBX Gallery source differs from the original fixture");
        } else
            atomic_write(root / path, source);
        const auto existing = AssetCatalog::open_project(root);
        const auto found = std::find_if(
            existing.records().begin(), existing.records().end(), [&](const auto& row) {
                return row.second.source == path && row.second.type == "model" &&
                       !row.second.subasset;
            });
        if (found != existing.records().end()) {
            const auto selected = load_model_selection(root, existing, found->first);
            check(selected.index.evidence_schema == "forge.fbx-model.v1" &&
                      selected.index.source_digest ==
                          content_digest(std::as_bytes(std::span(source))),
                  "Existing FBX Gallery publication differs from its source");
            models.emplace(name, found->first);
            continue;
        }
        AssetImportService service(lease, registry, {"windows", "d3d12", "desktop"}, 1);
        service.submit(
            service.prepare(path),
            [](auto& candidate, const auto& plan, const auto& catalog) {
                prepare_asset_publication(candidate, plan, catalog);
            },
            [](const auto&, const auto&) {});
        check(service.wait_idle(std::chrono::seconds(60)), "FBX Gallery import stalled");
        auto results = service.poll();
        check(results.size() == 1 && results[0].published,
              results.empty() ? "FBX Gallery import omitted receipt"
                              : results[0].diagnostic.c_str());
        models.emplace(name, results[0].job.asset);
        const auto selected =
            load_model_selection(root, results[0].publication->catalog, results[0].job.asset);
        added_records += selected.index.members.size() + 1;
    }
    auto catalog = AssetCatalog::open_project(root);
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(empty_scene());
    for (const auto& [file, name, x] :
         {std::tuple{"Static.fbx", "FBX - textured polygon mesh", -3.0},
          std::tuple{"Moving.fbx", "FBX - transform clip", 0.0},
          std::tuple{"Skin.fbx", "FBX - linear skin and bone clip", 3.0}}) {
        const auto selected = load_model_selection(root, catalog, models.at(file));
        ModelPlacementOptions options;
        options.name = name;
        options.source_scene = 0;
        options.transform.translation = {x - 2, -3, -4};
        for (const auto& member : selected.index.members)
            if (member.identity.type == "animation_clip")
                options.clip = {selected.bindings.at(member.identity.address)};
        const auto candidate =
            prepare_model_placement(selected, scene.asset_id(), scene.revision(), options);
        (void)instantiate_model(scene, catalog, candidate);
    }
    const auto create = [&](const char* name, unsigned kind, LocalTranslation p, LocalScale scale) {
        const auto id = authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}})
                            .at("selected")
                            .get<EntityId>();
        scene.entity(id.str()).set<LocalTranslation>(p).set<LocalScale>(scale);
        return id;
    };
    const auto floor = create("Display platform", 0, {0, -.15, -.3}, {9, .2f, 2});
    scene.entity(floor.str()).set<Tint>({.12f, .16f, .23f});
    const auto camera = create("Game camera", no_primitive, {0, 1.2, 9}, {1, 1, 1});
    Camera lens;
    lens.aspect = 16. / 9;
    lens.far_plane = 100;
    scene.entity(camera.str())
        .set<Camera>(lens)
        .set<LocalRotation>(rotation_from_euler({0, 180, 0}));
    const auto sun = create("Directional light", no_primitive, {0, 5, 4}, {1, 1, 1});
    Light light;
    light.intensity = 2.5f;
    scene.entity(sun.str()).set<Light>(light).set<LocalRotation>(rotation_from_euler({25, 155, 0}));
    scene.save(root / scene_path);
    catalog = AssetCatalog::open_project(root);
    const auto selected = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{selected.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    check(after.size() == before.size() + added_records,
          "FBX Gallery added an unexpected asset family");
    for (const auto& row : before)
        check(std::find(after.begin(), after.end(), row) != after.end(),
              "FBX Gallery changed an existing record");
}
Json inspect(const std::filesystem::path& project, AssetId scene_id,
             const std::map<AssetId, std::string>& roles) {
    RuntimeWorld world({}, {}, std::nullopt, project, false);
    world.scene.restore_snapshot(load_game_scene(project, {scene_id}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "FBX Gallery runtime assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto catalog = AssetCatalog::open_project(project);
    const auto sample = [&] {
        Json output = Json::object();
        const auto render = extract_render_scene(world.simulation.presentation(1));
        check(render.meshes.size() == 4 && render.cameras.size() == 1 && render.diagnostics.empty(),
              "FBX Gallery lost meshes/camera or reported presentation errors");
        const ModelSceneIndex index(render);
        for (const auto& mesh : render.meshes) {
            const auto* node = index.entity(mesh.entity);
            if (!node || !roles.contains(node->source.model.id))
                continue;
            const auto owner = node->source.model.id;
            const auto selected = load_model_selection(project, catalog, owner);
            const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
            const auto prepared = prepare_mesh_pose_geometry(resource.mesh);
            const auto pose = prepare_mesh_instance_pose(resource, prepared, mesh, index);
            const auto bounds = mesh_instance_bounds(pose, {0, 0, 0});
            const auto& name = roles.at(owner);
            output[name] = {bounds.minimum[1], bounds.maximum[1]};
            if (name == "Skin.fbx")
                check(bool(pose.lods.at(0).at(0).skin),
                      "FBX Gallery did not realize actual skin matrices");
        }
        check(output.size() == 3, "FBX Gallery lost a typed imported mesh");
        return output;
    };
    const auto initial = sample();
    for (unsigned i = 0; i < 30; ++i)
        world.simulation.tick(1.f / 60);
    const auto moved = sample();
    check(initial.at("Static.fbx") == moved.at("Static.fbx"), "Static FBX unexpectedly animated");
    for (const char* file : {"Moving.fbx", "Skin.fbx"}) {
        const auto delta = moved.at(file)[0].get<double>() - initial.at(file)[0].get<double>();
        check(delta > .8 && delta < 1.2, "FBX transform/weighted skin did not move by its clip");
    }
    return {{"initial", initial}, {"moved", moved}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 5,
              "Need --author Gallery worker converter or Gallery scratch worker converter");
        if (std::string_view(argv[1]) == "--author") {
            author(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                   std::filesystem::absolute(argv[4]));
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "Source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto scene = read(source / scene_path).at("asset_id").get<AssetId>();
        std::map<AssetId, std::string> roles;
        const auto catalog = AssetCatalog::open_project(source);
        for (const auto& [id, record] : catalog.records())
            if (record.type == "model" && record.source.parent_path() == source_folder)
                roles.emplace(id, record.source.filename().string());
        check(roles.size() == 3, "FBX Gallery has an unexpected model selection");
        const auto expected = inspect(source, scene, roles);
        const std::array roots{scene};
        package_runtime_content(source, scratch / "Content", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Content", scratch / "Relocated");
        std::filesystem::rename(source, scratch / "Source-offline");
        const auto actual = inspect(scratch / "Relocated", scene, roles);
        check(actual == expected, "Source-hidden FBX Gallery motion differs from authored content");
        check(!std::filesystem::exists(scratch / "Relocated/Assets"),
              "FBX runtime package contains editable sources");
        std::cout << "Original FBX Gallery typed models/material/image, actual transform/skin "
                     "movement and relocated source-hidden content passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
