#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/document.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/material_source.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
using namespace forge;
using namespace Diligent;
namespace {
using test_sprite_draw::check;
using Pixels = test_sprite_draw::Pixels;
Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    check(bool(file), "Transparency Gallery document missing");
    return Json::parse(file);
}
struct Prepared {
    std::filesystem::path scratch, root;
    AssetId scene;
    bool created{};
};
Prepared prepare(const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    Prepared result;
    result.scratch = scratch / AssetId::generate().str();
    result.root = result.scratch / "Project";
    std::filesystem::create_directories(result.scratch);
    std::filesystem::copy(gallery, result.root, std::filesystem::copy_options::recursive);
    const auto destination = result.root / "Scenes/114-weighted-transparency.scene.json";
    result.created = !std::filesystem::exists(destination);
    const auto before = AssetCatalog::open_project(result.root).document().at("assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(result.root, true);
    const auto settings = document.settings().document();
    if (result.created) {
        document.open_scene(result.root / "Scenes/113-projected-color-decals.scene.json");
        document.save_as(destination);
        authoring_command(scene, "scene.rendering.set", {{"weighted_transparency", true}});
        const auto original = scene.document();
        unsigned meshes = 0;
        const MaterialSource badge{read(result.root / "Assets/Sprites/Badge.material.json")};
        const std::array<std::array<float, 4>, 3> colors{
            {{.95f, .25f, .2f, .65f}, {.2f, .9f, .3f, .65f}, {.2f, .4f, 1.f, .65f}}};
        for (const auto& row : original.at("entities")) {
            const auto id = row.at("id");
            const auto& components = row.at("components");
            if (components.contains("forge.decal")) {
                authoring_command(scene, "entity.delete", {{"entity", id}});
                continue;
            }
            if (!components.contains("forge.mesh_renderer"))
                continue;
            const auto index = meshes++;
            if (!index)
                continue;
            check(index <= 3, "Transparency Gallery seed geometry changed");
            const auto set = [&](const char* component, const Json& values) {
                authoring_command(scene, "property.set_many",
                                  {{"entity", id}, {"component", component}, {"values", values}});
            };
            set("forge.primitive", {{"kind", 8}});
            set("forge.mesh_renderer",
                {{"mesh", engine_primitive(8).id},
                 {"materials", Json::array({{{"slot", "surface"}, {"material", badge.asset()}}})},
                 {"material_parameters",
                  Json::array({{{"slot", "surface"},
                                {"key", "baseColorFactor"},
                                {"parameter", {{"type", 5}, {"value", colors[index - 1]}}}}})},
                 {"cast_shadows", false},
                 {"layers", 3}});
            const auto rotation = rotation_from_euler({0, (double(index) - 2) * 55, 0});
            authoring_command(
                scene, "transform.local",
                {{"entity", id},
                 {"translation", {{"x", (double(index) - 2) * 1.25}, {"y", 1.5}, {"z", .5}}},
                 {"rotation",
                  {{"x", rotation.x}, {"y", rotation.y}, {"z", rotation.z}, {"w", rotation.w}}},
                 {"scale", {{"x", 4}, {"y", 3}, {"z", 1}}}});
            authoring_command(
                scene, "entity.rename",
                {{"entity", id},
                 {"name", std::array{"Red intersecting panel", "Green intersecting panel",
                                     "Blue intersecting panel"}[index - 1]}});
        }
        check(meshes == 4, "Transparency Gallery seed lost Meshes");
        document.save();
    } else {
        document.open_scene(destination);
    }
    result.scene = scene.asset_id();
    const auto extracted = extract_render_scene(scene.effective_document());
    check(extracted.settings.weighted_transparency && extracted.meshes.size() == 4 &&
              extracted.decals.empty() && extracted.cameras.size() == 1 &&
              extracted.diagnostics.empty(),
          "Transparency Gallery lost its saved rendering/geometry intent");
    const std::array roots{result.scene};
    prepare_runtime_content_catalog(*document.writer_guard(), roots, {});
    const auto after = AssetCatalog::open_project(result.root).document().at("assets");
    check(after.size() == before.size() + std::size_t(result.created) &&
              document.settings().document() == settings,
          "Transparency Gallery changed unrelated asset counts or project settings");
    for (const auto& row : before)
        check(std::find(after.begin(), after.end(), row) != after.end(),
              "Transparency Gallery modified an existing asset record");
    return result;
}
class Capture {
  public:
    Capture(DiligentPresentation& p, IDeviceContext* c, const std::filesystem::path& root,
            const AssetCatalog& catalog, bool preview = false)
        : presentation(p), context(c),
          host(std::make_shared<MeshResourceHost>(p, c, root, preview)), frame(p) {
        host->catalog(std::make_shared<const AssetCatalog>(catalog));
        frame.resources(host);
    }
    Pixels image(const RenderScene& scene, unsigned width, unsigned height, bool fallback = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* output = frame.game(context, scene, width, height);
            check(output, "Transparency Gallery has no display target");
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, output->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline, "Transparency Gallery stalled");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        check(frame.omitted_diagnostics() == 0, "Transparency Gallery diagnostics truncated");
        bool warned = false;
        for (const auto& diagnostic : frame.diagnostics()) {
            if (fallback && diagnostic.category == "render.transparency.unavailable" &&
                diagnostic.context.property == "rendering.weighted_transparency")
                warned = true;
            else
                throw std::runtime_error(diagnostic.text);
        }
        check(!fallback || warned, "Sorted transport fallback lacks its contextual warning");
        return pixels;
    }
    DiligentPresentation& presentation;
    IDeviceContext* context;
    std::shared_ptr<MeshResourceHost> host;
    FrameRenderer frame;
};
std::vector<Pixels> render(DiligentPresentation& presentation, IDeviceContext* context,
                           const std::filesystem::path& root, const AssetCatalog& catalog,
                           AssetId id, const std::filesystem::path& captures, bool boundaries) {
    EngineContext engine(WorldRole::Runtime, false);
    Scene runtime(engine.world());
    runtime.restore_snapshot(read(root / catalog.records().at(id).source));
    const auto scene = extract_render_scene(runtime.effective_document());
    Capture capture(presentation, context, root, catalog);
    std::filesystem::create_directories(captures);
    std::vector<Pixels> result;
    for (auto [width, height] : {std::pair{640u, 360u}, {360u, 640u}, {96u, 64u}, {17u, 13u}}) {
        auto pixels = capture.image(scene, width, height);
        check(std::none_of(pixels.begin(), pixels.end(),
                           [](auto p) { return p[0] == 255 && p[1] == 0 && p[2] == 255; }),
              "Transparency Gallery reached the rejection color");
        if (boundaries && width == 640) {
            auto sorted = scene;
            sorted.settings.weighted_transparency = false;
            const auto ordinary = capture.image(sorted, width, height);
            unsigned changed = 0;
            for (std::size_t i = 0; i < pixels.size(); ++i)
                changed += pixels[i] != ordinary[i];
            check(changed > 100, "Weighted Gallery does not demonstrate overlapping transparency");
            check(capture.image(scene, width, height) == pixels,
                  "Transparency toggle did not recover");
        }
        std::ofstream file(captures /
                               (std::to_string(width) + "x" + std::to_string(height) + ".ppm"),
                           std::ios::binary);
        file << "P6\n" << width << ' ' << height << "\n255\n";
        for (const auto& pixel : pixels)
            file.write(reinterpret_cast<const char*>(pixel.data()), 3);
        check(bool(file), "Transparency capture failed");
        result.push_back(std::move(pixels));
    }
    if (boundaries) {
        Capture transport(presentation, context, root, catalog, true);
        const AssetRef<MaterialAsset> badge{AssetId::parse("7949266a-8b55-4b6e-beb6-37526ca79446")};
        MaterialData values;
        values.model = "forge.gltf.metallic-roughness.v1";
        values.alpha = MaterialAlpha::Blend;
        values.depth_write = false;
        values.double_sided = true;
        values.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.8f}};
        values.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
        values.parameters["thicknessFactor"] = {MaterialParameterType::Scalar, {.25f}};
        transport.host->preview_material(badge, {values, {}, {}});
        auto sorted = scene;
        sorted.settings.weighted_transparency = false;
        const auto ordinary = transport.image(sorted, 96, 64);
        check(transport.image(scene, 96, 64, true) == ordinary,
              "Transmission did not retain exact whole-view sorted HDR transport");
    }
    context->WaitForIdle();
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--prepare-only"),
              "Need Gallery and scratch");
        const auto prepared = prepare(argv[1], argv[2]);
        std::cout << Json{{"project", prepared.root.string()},
                          {"scene", prepared.scene},
                          {"created", prepared.created}}
                         .dump()
                  << std::endl;
        if (argc == 4)
            return 0;
        const auto catalog = AssetCatalog::open_project(prepared.root);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        info.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        info.Features.IndependentBlend = DEVICE_FEATURE_STATE_DISABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Gallery native device unavailable");
        DiligentPresentation presentation(device);
        const auto original = render(presentation, context, prepared.root, catalog, prepared.scene,
                                     prepared.scratch / "authored", true);
        const std::array roots{prepared.scene};
        const auto package = prepared.scratch / "Export";
        (void)package_runtime_content(prepared.root, package, roots, {"windows", "d3d12"});
        const auto relocated = prepared.scratch / "Relocated";
        std::filesystem::rename(package, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        std::set<AssetId> actual;
        for (const auto& [id, record] : shipped.records()) {
            (void)record;
            actual.insert(id);
        }
        check(actual == std::set<AssetId>{prepared.scene,
                                          AssetId::parse("7949266a-8b55-4b6e-beb6-37526ca79446"),
                                          AssetId::parse("0c07b499-c865-4e5b-838d-8021bb7a99d3")},
              "Transparency export closure changed");
        std::filesystem::rename(prepared.root, prepared.scratch / "Source-offline");
        const auto exported = render(presentation, context, relocated, shipped, prepared.scene,
                                     prepared.scratch / "relocated", false);
        check(original == exported, "Source-free relocated transparency display changed");
        check(!std::filesystem::exists(relocated / "Assets/Sprites/Badge.material.json"),
              "Material source leaked into runtime export");
        std::cout << "Gallery114/save, actual HDR/display, transmission fallback and four exact "
                     "source-free relocated captures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
