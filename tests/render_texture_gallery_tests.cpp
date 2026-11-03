#include "render_texture_fixture.hpp"
#include "render_texture_plan.hpp"
#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <numbers>
#include <thread>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/152-camera-render-textures.scene.json";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Camera texture Gallery already exists");
    std::filesystem::create_directories(root / "Assets/RenderTextures");
    const auto lease = std::make_shared<ProjectLease>(root);
    const auto output = [&](const char* path) {
        auto doc = RenderTextureDocument::create(lease, path, [](AssetId id) {
            auto source = RenderTextureSource::create(id);
            source.document["width"] = 512;
            source.document["height"] = 256;
            return source;
        });
        test_render_texture::publish(lease, *doc, render_texture_import_registry(),
                                     prepare_render_texture_publication);
        return doc->source().asset();
    };
    const auto split = output("Assets/RenderTextures/Split.rendertexture.json");
    const auto relay = output("Assets/RenderTextures/Relay.rendertexture.json");
    const auto first =
        test_render_texture::material(lease, "Assets/RenderTextures/Split.material.json", split);
    const auto second =
        test_render_texture::material(lease, "Assets/RenderTextures/Relay.material.json", relay);
    const auto solid =
        test_render_texture::material(lease, "Assets/RenderTextures/Color.material.json");
    auto clip =
        TransformAnimationDocument::create(lease, "Assets/RenderTextures/Spin.animation.json");
    clip->edit(clip->revision(), "Two-second rotation", [](Json& json) {
        TransformAnimationSource source{json};
        source.document["channels"] = {"forge.local_rotation"};
        for (unsigned index = 1; index <= 4; ++index) {
            const double half = index * std::numbers::pi / 4;
            LocalTransform pose;
            pose.rotation = {0, float(std::sin(half)), 0, float(std::cos(half))};
            source.set_key(index * animation_timebase / 2, pose);
        }
        json = source.document;
    });
    test_render_texture::publish(lease, *clip, transform_animation_import_registry(),
                                 prepare_transform_animation_publication);
    EngineContext engine;
    Scene scene(engine.world());
    const auto entity = [&](const char* name, Double3 position) {
        const auto id =
            authoring_command(scene, "entity.create", {{"kind", no_primitive}, {"name", name}})
                .at("selected")
                .get<std::string>();
        scene.entity(id).set<LocalTranslation>({position[0], position[1], position[2]});
        return id;
    };
    for (unsigned index = 0; index < 2; ++index) {
        const auto id = entity(index ? "Cyan cube - rotating source" : "Red cube - rotating source",
                               {index ? 2. : -2., 0, 3});
        MeshRenderer mesh;
        mesh.mesh = engine_primitive(0);
        mesh.materials = {{"surface", {solid}}};
        mesh.layers = 1;
        mesh.material_parameters = {{"surface",
                                     "baseColorFactor",
                                     {MaterialParameterType::LinearColor4,
                                      index ? std::array<float, 4>{.05f, .7f, 1.f, 1.f}
                                            : std::array<float, 4>{1.f, .1f, .05f, 1.f}}}};
        scene.entity(id).set(mesh).set(TransformAnimator{{clip->source().asset()}});
    }
    const auto camera = [&](const char* name, Double3 position, AssetId target,
                            std::uint32_t layers, double height) {
        const auto id = entity(name, position);
        Camera lens;
        lens.render_texture = {target};
        lens.layers = layers;
        lens.projection = unsigned(CameraProjection::Orthographic);
        lens.orthographic_height = height;
        scene.entity(id).set(lens);
        return id;
    };
    const auto left = camera("Split left - red source", {-2, 0, -1}, split, 1, 2.5);
    auto lens = scene.entity(left).get<Camera>();
    lens.viewport_width = .5;
    scene.entity(left).set(lens);
    const auto right = camera("Split right - cyan source", {2, 0, -1}, split, 1, 2.5);
    lens.viewport_x = .5;
    lens.order = 1;
    scene.entity(right).set(lens);
    const auto monitor = [&](const char* name, double x, AssetId material, std::uint32_t layers) {
        const auto id = entity(name, {x, 0, 0});
        MeshRenderer mesh;
        mesh.mesh = engine_primitive(8);
        mesh.materials = {{"surface", {material}}};
        mesh.layers = layers;
        mesh.cast_shadows = false;
        scene.entity(id).set(mesh).set<LocalScale>({3, 1.5, 1});
    };
    monitor("Monitor A - two cameras share Split", -2.2, first, 4);
    camera("Relay - reads Monitor A", {-2.2, 0, -4}, relay, 4, 1.5);
    monitor("Monitor B - same-frame Relay", 2.2, second, 8);
    const auto main = camera("Main - two monitors", {0, 0, -8}, {}, 12, 4.5);
    auto main_lens = scene.entity(main).get<Camera>();
    main_lens.background_r = .08f;
    main_lens.background_g = .1f;
    main_lens.background_b = .14f;
    scene.entity(main).set(main_lens);
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise(const std::filesystem::path& root, const Json& source) {
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    scene.restore_snapshot(source);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline,
              "Camera Gallery animation admission timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    const auto render = extract_render_scene(scene.effective_document());
    check(render.meshes.size() == 4 && render.cameras.size() == 4,
          "Camera Gallery lost its actors");
    std::map<AssetId, RenderTextureData> definitions;
    AssetId split, relay;
    EntityId dependent;
    for (const auto& camera : render.cameras)
        if (const auto id = camera.camera.render_texture.id) {
            definitions[id] = {512, 256};
            if (camera.camera.layers == 1)
                split = id;
            else {
                relay = id;
                dependent = camera.entity;
            }
        }
    check(bool(split) && bool(relay) && split != relay, "Gallery output identities overlap");
    const auto plan =
        prepare_render_texture_plan(render, definitions, {{dependent, {split}}}, {split, relay});
    check(plan.order == std::vector<AssetId>{split, relay}, "Gallery relay precedes its producer");
    const auto& cameras = plan.passes.at(split).composition.cameras;
    check(cameras.size() == 2 && cameras[0].view.viewport == PixelViewport{0, 0, 256, 256} &&
              cameras[1].view.viewport == PixelViewport{256, 0, 256, 256} &&
              prepare_game_cameras(render, 1280, 720).cameras.size() == 1,
          "Gallery split viewports leaked into the main output");
    const auto before = scene.effective_document();
    simulation.tick(.25f);
    const auto after = scene.effective_document();
    unsigned rotating = 0;
    for (const auto& row : after.at("entities"))
        if (row.at("components").contains("forge.transform_animator")) {
            const auto& components = row.at("components");
            const auto& initial =
                *std::find_if(before.at("entities").begin(), before.at("entities").end(),
                              [&](const auto& v) { return v.at("id") == row.at("id"); });
            check(components.at("forge.local_rotation") !=
                          initial.at("components").at("forge.local_rotation") &&
                      components.at("forge.local_translation") ==
                          initial.at("components").at("forge.local_translation") &&
                      components.at("forge.local_scale") ==
                          initial.at("components").at("forge.local_scale"),
                  "Gallery animation did not rotate in place");
            ++rotating;
        }
    check(rotating == 2, "Gallery rotation tracks missing");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch root, or --author and Gallery root");
        if (std::string_view(argv[1]) == "--author") {
            author(argv[2]);
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / scene_path);
        exercise(root, source);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        const auto destination = scratch / "export";
        package_runtime_content(root, destination, roots, {"windows", "d3d12"});
        const auto moved = scratch / "relocated";
        std::filesystem::rename(destination, moved);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        check(catalog.records().size() == 7, "Camera Gallery lost or duplicated its saved closure");
        std::filesystem::rename(root, scratch / "source-offline");
        exercise(moved, load_game_scene(moved, {roots[0]}));
        std::cout << "Camera Gallery split composition, dependent output, rotating sources and "
                     "relocated seven-asset closure passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
