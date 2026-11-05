#include "material_authoring.hpp"
#include "material_document.hpp"
#include "model_render_resource.hpp"
#include "paint_texture_document.hpp"
#include "runtime_package.hpp"
#include "texture_authoring.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/170-texture-painting.scene.json";
constexpr auto image_path = "Assets/Painting/Signal.paint-texture.json";
constexpr auto material_path = "Assets/Painting/Signal.material.json";
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
PaintTextureValue signal(AssetId id) {
    auto value = PaintTextureValue::create(id, 128, 128, TextureSemantic::Color, {14, 20, 30, 255});
    PaintBrush brush;
    brush.radius = 5;
    brush.hardness = 1;
    brush.color = {1, .45, .04, 1};
    const std::array<PaintPoint, 5> border{
        {{12.5, 12.5}, {115.5, 12.5}, {115.5, 115.5}, {12.5, 115.5}, {12.5, 12.5}}};
    value = value.paint(border, brush);
    brush.radius = 8;
    brush.color = {.04, .8, 1, 1};
    const std::array<PaintPoint, 3> mark{{{35.5, 86.5}, {35.5, 39.5}, {74.5, 39.5}}};
    value = value.paint(mark, brush);
    brush.radius = 6;
    brush.color = {.85, .04, .4, 1};
    const std::array<PaintPoint, 2> slash{{{53.5, 86.5}, {90.5, 54.5}}};
    value = value.paint(slash, brush);
    brush.radius = 4;
    brush.color = {1, 1, 1, 1};
    for (const auto point : {PaintPoint{88.5, 31.5}, PaintPoint{102.5, 31.5}})
        value = value.paint(std::span(&point, 1), brush);
    return value;
}
template <class Document, class Prepare>
void publish(std::shared_ptr<ProjectLease> lease, Document& source,
             std::shared_ptr<const AssetImporterRegistry> registry, Prepare prepare) {
    source.save();
    AssetImportService service(lease, std::move(registry), {"windows", "d3d12", "desktop"}, 1);
    service.submit(
        service.prepare(source.locator(), {}, source.source().asset()),
        [prepare](auto& candidate, const auto& plan, const auto&) { prepare(candidate, plan); },
        [](const auto&, const auto&) {});
    check(service.wait_idle(std::chrono::seconds(30)), "Paint Gallery publication timeout");
    const auto receipts = service.poll();
    if (receipts.size() != 1 || !receipts[0].published)
        throw std::runtime_error(receipts.empty() ? "Missing paint publication"
                                                  : receipts[0].diagnostic);
}
void author(const std::filesystem::path& root, const std::filesystem::path& worker) {
    check(!std::filesystem::exists(root / scene_path), "Paint Gallery already exists");
    const auto before = AssetCatalog::open_project(root).document().at("assets");
    std::filesystem::create_directories(root / "Assets/Painting");
    const auto lease = std::make_shared<ProjectLease>(root);
    const bool image_exists = std::filesystem::exists(root / image_path);
    auto image = image_exists ? std::make_unique<PaintTextureDocument>(lease, image_path)
                              : PaintTextureDocument::create(lease, image_path, [](AssetId id) {
                                    return PaintTextureSource{signal(id)};
                                });
    check(image->source().document == signal(image->source().asset()),
          "Existing paint fixture source differs");
    const auto image_id = image->source().asset();
    if (!image_exists)
        publish(lease, *image, texture_import_registry(worker), prepare_texture_publication);
    else
        check(AssetCatalog::open_project(root).resolve(AssetRef<TextureAsset>{image_id}).state ==
                  AssetState::Available,
              "Existing paint fixture needs publication recovery");
    const bool material_exists = std::filesystem::exists(root / material_path);
    auto material =
        material_exists
            ? std::make_unique<MaterialDocument>(lease, material_path)
            : MaterialDocument::create(lease, material_path, [&](AssetId id) {
                  auto source = MaterialSource::create(id);
                  auto& values = source.document["overrides"];
                  values["model"] = "forge.gltf.unlit.v1";
                  values["double_sided"] = true;
                  MaterialData shape;
                  shape.model = "forge.gltf.unlit.v1";
                  shape.textures["baseColorTexture"].semantic = TextureSemantic::Color;
                  shape.textures["baseColorTexture"].sampler.u = TextureWrap::ClampEdge;
                  shape.textures["baseColorTexture"].sampler.v = TextureWrap::ClampEdge;
                  values["textures"]["baseColorTexture"] = {
                      {"asset", image_id},
                      {"slot",
                       material_values_document(shape).at("textures").at("baseColorTexture")}};
                  return source;
              });
    const auto material_id = material->source().asset();
    if (!material_exists)
        publish(lease, *material, material_import_registry(), prepare_material_publication);
    else
        check(material->source()
                          .document.at("overrides")
                          .at("textures")
                          .at("baseColorTexture")
                          .at("asset")
                          .get<AssetId>() == image_id &&
                  AssetCatalog::open_project(root)
                          .resolve(AssetRef<MaterialAsset>{material_id})
                          .state == AssetState::Available,
              "Existing paint fixture Material differs or is not published");
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, unsigned kind, LocalTranslation position,
                            LocalScale scale) {
        const auto id = authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}})
                            .at("selected")
                            .get<std::string>();
        scene.entity(id).set<LocalTranslation>(position).set<LocalScale>(scale);
        return id;
    };
    const auto cube =
        create("Painted cube - shared spin clip", no_primitive, {-2, 0, 2}, {2, 2, 2});
    MeshRenderer renderer;
    renderer.mesh = engine_primitive(0);
    renderer.materials = {{"surface", {material_id}}};
    scene.entity(cube).set<MeshRenderer>(renderer);
    TransformAnimator animation;
    animation.clip = {
        read(root / "Assets/RenderTextures/Spin.animation.json").at("asset_id").get<AssetId>()};
    scene.entity(cube).set<TransformAnimator>(animation);
    const auto panel =
        create("Painted Sprite - same Texture and Material", no_primitive, {2, 0, 2}, {1, 1, 1});
    SpriteRenderer sprite;
    sprite.material = {material_id};
    sprite.width = sprite.height = 3;
    scene.entity(panel).set<SpriteRenderer>(sprite);
    const auto floor = create("Floor", 0, {0, -1.7, 3}, {14, .2, 12});
    scene.entity(floor).set<Tint>({.14f, .18f, .24f});
    const auto camera = create("Game camera", no_primitive, {0, 1, -9}, {1, 1, 1});
    Camera lens;
    lens.aspect = 16. / 9;
    lens.far_plane = 100;
    scene.entity(camera).set<Camera>(lens).set<LocalRotation>(rotation_from_euler({4, 0, 0}));
    const auto sun = create("Directional light", no_primitive, {0, 5, -4}, {1, 1, 1});
    Light light;
    light.intensity = 2;
    scene.entity(sun).set<Light>(light).set<LocalRotation>(rotation_from_euler({35, -25, 0}));
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto selected = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{selected.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    check(after.size() == before.size() + 1 + unsigned(!image_exists) + unsigned(!material_exists),
          "Paint Gallery changed unrelated asset count");
    for (const auto& row : before)
        check(std::find(after.begin(), after.end(), row) != after.end(),
              "Paint Gallery changed an existing asset record");
}
Json inspect(const std::filesystem::path& root, AssetId scene_id, AssetId image_id) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene_id}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline,
              "Paint Gallery runtime resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto frame = extract_render_scene(world.simulation.presentation(1));
    check(frame.meshes.size() == 2 && frame.sprites.size() == 1 && frame.cameras.size() == 1 &&
              frame.diagnostics.empty(),
          "Paint Gallery lost ordinary Mesh/Sprite/camera presentation");
    const auto catalog = AssetCatalog::open_project(root);
    const auto texture = asset_detail::load_texture_selection(root, catalog, {image_id});
    const auto expected = signal(image_id).texture();
    check(texture.value->width == 128 && texture.value->height == 128 &&
              texture.value->subresources[0] == expected.subresources[0],
          "Paint Gallery cooked texture lost original painted base pixels");
    const auto material =
        asset_detail::load_pbr_material(root, catalog, frame.sprites[0].renderer.material);
    check(material.textures.at("baseColorTexture").id == image_id,
          "Sprite material lost painted Texture identity");
    return {{"pixels", asset_detail::content_digest(texture.value->subresources[0])},
            {"material", frame.sprites[0].renderer.material.id}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need --author Gallery worker or Gallery scratch worker");
        if (std::string_view(argv[1]) == "--author") {
            author(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto scene_id = read(source / scene_path).at("asset_id").get<AssetId>();
        const auto image_id = read(source / image_path).at("asset_id").get<AssetId>();
        const auto expected = inspect(source, scene_id, image_id);
        const std::array roots{scene_id};
        package_runtime_content(source, scratch / "export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "export", scratch / "relocated");
        std::filesystem::rename(source, scratch / "source-offline");
        check(inspect(scratch / "relocated", scene_id, image_id) == expected,
              "Source-hidden relocated paint resources differ");
        std::cout << "Paint Gallery original pixels and shared Mesh/Sprite Material survive "
                     "source-hidden relocated Windows-target content on Linux\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
