#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_asset_publication.hpp"
#include "material_authoring.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_package.hpp"
#include "shader_pipeline.hpp"
#include "ui_effect_fixture.hpp"
#include "ui_inspection.hpp"
#ifdef FORGE_GALLERY_SHADER_COOK
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#endif
#include <algorithm>
#include <array>
#include <forge/authoring.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_components.hpp>
#include <forge/surface_shader.hpp>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>
#include <set>

using namespace forge;
namespace {
struct GalleryExample {
    const char* scene;
    const char* shader;
    const char* document;
    std::array<const char*, 2> materials;
};
constexpr std::array examples{GalleryExample{"Scenes/213-ui-material-effects.scene.json",
                                             "Assets/UI/MaterialEffects/Panel.shader.json",
                                             "Assets/UI/MaterialEffects/Lab.rml",
                                             {"Assets/UI/MaterialEffects/Still.material.json",
                                              "Assets/UI/MaterialEffects/Motion.material.json"}},
                              GalleryExample{"Scenes/214-ui-material-graphs.scene.json",
                                             "Assets/UI/GraphEffects/Panel.shader.json",
                                             "Assets/UI/GraphEffects/Lab.rml",
                                             {"Assets/UI/GraphEffects/Still.material.json",
                                              "Assets/UI/GraphEffects/Motion.material.json"}}};

void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
SurfaceShaderDefinition definition() {
    SurfaceShaderDefinition value;
    value.ui_effect = true;
    value.uv_sets = {0};
    value.parameters["tint"] = {MaterialParameterType::LinearColor4, {1, 1, 1, 1}};
    value.parameters["speed"] = {MaterialParameterType::Scalar, {0}};
    value.labels = {{"tint", "Tint"}, {"speed", "Motion speed"}};
    value.textures["image"].semantic = TextureSemantic::Color;
    return value;
}
void author(const std::filesystem::path& root, bool graph = false) {
    const auto& example = examples[graph ? 1 : 0];
    check(!std::filesystem::exists(root / example.scene), "UI Material Gallery already exists");
    const auto directory = (root / example.shader).parent_path();
    std::filesystem::create_directories(graph ? directory : directory / "Shaders");
    auto catalog = AssetCatalog::open_project(root);
    AssetId texture;
    for (const auto& [id, record] : catalog.records())
        if (record.source == "Assets/Sprites/Badge.png" && record.type == "texture")
            texture = id;
    check(bool(texture), "UI Material Gallery requires the published Badge Texture");
    const auto shader = AssetId::generate();
    Json declaration{
        {"format", "forge.shader"},
        {"version", 2},
        {"asset_id", shader},
        {"source_root", "Assets/UI/MaterialEffects/Shaders"},
        {"surface", surface_definition_document(definition())},
        {"stages",
         Json::array({{{"stage", "pixel"}, {"source", "panel.hlsl"}, {"entry", "GalleryPanel"}}})}};
    if (graph) {
        declaration = test::ui_effect_graph_document(shader);
        const std::array<std::array<double, 2>, 19> positions{{{1500, 600},
                                                               {2100, 360},
                                                               {0, 40},
                                                               {300, 40},
                                                               {300, 220},
                                                               {0, 430},
                                                               {0, 600},
                                                               {300, 430},
                                                               {600, 40},
                                                               {900, 40},
                                                               {0, 820},
                                                               {300, 820},
                                                               {300, 1000},
                                                               {600, 820},
                                                               {900, 820},
                                                               {1200, 250},
                                                               {1200, 600},
                                                               {1500, 250},
                                                               {1800, 360}}};
        auto& nodes = declaration["graph"]["nodes"];
        check(nodes.size() == positions.size(), "Gallery graph layout no longer matches its nodes");
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            nodes[i]["position"] = positions[i];
            if (nodes[i]["data"].value("key", std::string{}) == "speed")
                nodes[i]["data"]["label"] = "Motion speed";
        }
    }
    (void)shader_program_source(declaration);
    atomic_write(root / example.shader, declaration.dump(2));
    if (!graph)
        atomic_write(directory / "Shaders/panel.hlsl", R"hlsl(#include "engine/forge.surface.hlsli"
float4 GalleryPanel(ForgeUiInput input) {
    float speed = ForgeParameter_speed();
    float band = 0.65 + 0.35 * cos(6.2831853 * (input.UV.x - input.Time * speed));
    float4 image = ForgeSample_image(input);
    return float4(image.rgb * ForgeParameter_tint().rgb * band,
                  image.a * ForgeParameter_tint().a);
}
)hlsl");
    catalog.add({shader, "shader", example.shader, 1, {}});
    std::array<AssetId, 2> materials;
    for (unsigned i = 0; i < materials.size(); ++i) {
        auto material = MaterialSource::create(AssetId::generate());
        materials[i] = material.asset();
        material.document["version"] = 2;
        auto& overrides = material.document["overrides"];
        overrides["shader"] = shader;
        overrides["parameters"]["speed"] = {{"type", unsigned(MaterialParameterType::Scalar)},
                                            {"value", {i ? .25 : 0.}}};
        auto slot = surface_material_defaults(definition()).textures.at("image");
        if (i) {
            slot.offset = {1, 0};
            slot.scale = {-1, 1};
        }
        auto values = surface_material_defaults(definition());
        values.textures["image"] = slot;
        overrides["textures"]["image"] = {
            {"asset", texture},
            {"slot", material_values_document(values).at("textures").at("image")}};
        material.validate();
        atomic_write(root / example.materials[i], material.document.dump(2));
        catalog.add({materials[i], "material", example.materials[i], 1, {}});
    }
    catalog.save(AssetCatalog::project_index(root));
    const std::string stylesheet = graph ? "../MaterialEffects/Lab.rcss" : "Lab.rcss";
    const std::string intro = graph ? "UVs, presentation time and element size feed these "
                                      "graph-authored textured gradients."
                                    : "The same image uses a still band on the left and a moving, "
                                      "flipped band on the right.";
    atomic_write(root / example.document,
                 "<rml><head><link type=\"text/rcss\" href=\"" + stylesheet +
                     "\" /></head>"
                     "<body><div id=\"lab\"><p class=\"eyebrow\">FORGE / UI MATERIALS</p>"
                     "<h1>One Shader. Two Materials.</h1>"
                     "<p>" +
                     intro +
                     "</p>"
                     "<div class=\"comparison\"><div class=\"card\"><h2>Still</h2>"
                     "<div class=\"clip\"><div id=\"still\" style='decorator:shader(\"material:" +
                     materials[0].str() +
                     "\");' /></div><p>Speed 0 / normal UVs</p></div>"
                     "<div class=\"card\"><h2>Motion</h2><div class=\"clip\">"
                     "<div id=\"motion\" style='decorator:shader(\"material:" +
                     materials[1].str() +
                     "\");' /></div><p>Speed 0.25 / flipped UVs / opacity 75%</p></div></div>"
                     "<p>Stop Play. Open Still.material.json or Motion.material.json in Content. "
                     "Change Motion speed, Tint or image UVs, Save, then restart Play.</p>"
                     "<p>Presentation time continues while simulation is paused. No C++ module is "
                     "required.</p>"
                     "</div></body></rml>\n");
    if (!graph)
        atomic_write(directory / "Lab.rcss", R"rcss(body,div,p,h1,h2 {display:block;}
body {width:100%;height:100%;font-family:Lato;font-size:17px;color:#e8f1f6;}
#lab {position:absolute;left:4%;top:4%;width:92%;max-width:920px;max-height:92%;box-sizing:border-box;padding:24px;background-color:#162a39;overflow:auto;}
.eyebrow {font-size:13px;color:#70d7bd;} h1 {font-size:28px;} h2 {font-size:21px;}
p {margin:12px 0;line-height:1.35;} .comparison {display:flex;gap:24px;flex-wrap:wrap;}
.card {width:300px;} .clip {width:280px;height:160px;overflow:hidden;background-color:#0a1a27;}
#still,#motion {width:320px;height:180px;} #motion {opacity:0.75;}
scrollbarvertical {width:10px;background-color:#0a1a27;} scrollbarvertical sliderbar {background-color:#477b90;}
)rcss");
    const auto document = register_ui_document(root, example.document);
    auto lease = std::make_shared<ProjectLease>(root);
    catalog = AssetCatalog::open_project(root);
    if (!graph)
        catalog.add(
            {AssetId::generate(), "ui_stylesheet", "Assets/UI/MaterialEffects/Lab.rcss", 1, {}});
    catalog.save(AssetCatalog::project_index(root));
    declare_runtime_dependencies(
        *lease, document.id,
        {{materials[0], "material", AssetDependencyKind::Runtime, "declared:Still panel", {}},
         {materials[1], "material", AssetDependencyKind::Runtime, "declared:Motion panel", {}}},
        catalog.document());
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name) {
        auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                       .at("selected")
                                       .get<std::string>());
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    auto camera = create("Game Camera");
    camera.set<Camera>({});
    camera.set<LocalTranslation>({0, 0, -8});
    create("UI Material comparison").set<UiDocument>({{document.id}, true, true, 0});
    scene.save(root / example.scene);
    catalog = AssetCatalog::open_project(root);
    catalog.add_scene(example.scene);
    catalog.save(AssetCatalog::project_index(root));
}
void inspect(const std::filesystem::path& root, bool graph = false) {
    const auto& example = examples[graph ? 1 : 0];
    const auto shader = read(root / example.shader);
    const auto captured = asset_detail::capture_shader_source(root, example.shader);
    check(graph ? (captured.project_sources.empty() &&
                   captured.sources.contains("engine/forge.graph.hlsl"))
                : (captured.project_sources.size() == 1 && captured.sources.contains("panel.hlsl")),
          "Gallery UI Shader lost its captured HLSL source");
    const auto surface = *captured.program.surface;
    check(surface.ui_effect && surface_definition_document(surface).at("version") == 5 &&
              surface.textures.size() == 1 &&
              surface.textures.at("image").semantic == TextureSemantic::Color,
          "Gallery UI Shader lost its ordinary Color Texture interface");
    const auto catalog = AssetCatalog::open_project(root);
    std::array<AssetId, 2> materials;
    for (unsigned i = 0; i < materials.size(); ++i) {
        const auto source = MaterialSource::parse(
            asset_detail::read_bytes(root / example.materials[i], material_source_byte_limit));
        materials[i] = source.asset();
        const auto& overrides = source.document.at("overrides");
        check(overrides.at("shader").get<AssetId>() == shader.at("asset_id").get<AssetId>() &&
                  overrides.at("parameters").at("speed").at("value").at(0) == (i ? .25 : 0.),
              "Gallery UI Material lost its shared Shader or saved speed");
        const auto texture = overrides.at("textures").at("image").at("asset").get<AssetId>();
        const auto& slot = overrides.at("textures").at("image").at("slot");
        check(slot.at("scale").at(0) == (i ? -1. : 1.) &&
                  slot.at("offset").at(0) == (i ? 1. : 0.) &&
                  catalog.records().at(materials[i]).source == example.materials[i],
              "Gallery UI Material identity or saved UV transform differs");
        check(catalog.records().at(texture).source == "Assets/Sprites/Badge.png",
              "Gallery UI Material does not use the real imported Badge Texture");
    }
    const auto scene_source = read(root / example.scene);
    WorldContext world;
    Scene scene(world);
    scene.load(root / example.scene);
    const auto& records = catalog.records();
    check(records.at(scene.asset_id()).source == example.scene &&
              scene_source.at("entities").size() == 2 &&
              std::count_if(scene_source.at("entities").begin(), scene_source.at("entities").end(),
                            [](const auto& row) {
                                return row.at("components").contains("forge.camera");
                            }) == 1,
          "Gallery UI Scene identity or camera/HUD composition differs");
    AssetId document;
    for (const auto& row : scene_source.at("entities"))
        if (row.at("components").contains("forge.ui_document"))
            document = row.at("components").at("forge.ui_document").at("document").get<AssetId>();
    check(bool(document) && records.at(document).source == example.document,
          "Gallery Scene lost its UI document");
    for (auto id : materials)
        check(std::any_of(records.at(document).dependency_edges.begin(),
                          records.at(document).dependency_edges.end(),
                          [&](const auto& edge) {
                              return edge.target == id &&
                                     edge.kind == AssetDependencyKind::Runtime &&
                                     edge.expected_type == "material";
                          }),
              "Gallery UI Material runtime dependency missing");
    std::cout
        << "UI Material Gallery sources, identities, Texture and export declarations admitted\n";
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker,
             const std::filesystem::path& font, bool graph = false) {
#ifdef FORGE_GALLERY_SHADER_COOK
    const auto& example = examples[graph ? 1 : 0];
    inspect(root, graph);
    auto lease = std::make_shared<ProjectLease>(root);
    const auto directory = worker.parent_path();
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(asset_detail::shader_importer(directory / "forge_shader_build.exe",
                                                {asset_detail::diligent_shader_compiler_digest(),
                                                 asset_detail::diligent_shader_compiler_debug()}));
    registry->seal();
    AssetImportService shaders(lease, registry, {"windows-x64", "d3d12", "fxc-5.1"});
    test::publish_gallery_asset(
        shaders, example.shader, prepare_shader_publication,
        std::chrono::seconds(asset_detail::shader_worker_limits().seconds + 15));
    AssetImportService materials(lease, material_import_registry(),
                                 {"windows", "d3d12", "desktop"});
    for (auto path : example.materials)
        test::publish_gallery_asset(materials, path, prepare_material_publication);
    WorldContext world;
    Scene scene(world);
    scene.load(root / example.scene);
    const std::array roots{scene.asset_id()};
    const auto inspect_ui = [&](AssetId id, std::stop_token stop) {
        return inspect_ui_dependencies(directory / "forge_ui_inspect.exe", font, root, id, stop);
    };
    // Publication makes the UI Materials available to native source inspection.
    // Review the full observed stylesheet set before checking export declarations.
    const auto catalog = AssetCatalog::open_project(root);
    AssetId document;
    for (const auto& [id, record] : catalog.records())
        if (record.source == example.document)
            document = id;
    check(bool(document), "UI Material Gallery document missing after publication");
    const auto observed = inspect_ui(document, {});
    std::vector<AssetDependency> declarations;
    for (const auto& edge : catalog.records().at(document).dependency_edges)
        if (declared_runtime_edge(edge))
            declarations.push_back(edge);
    declare_runtime_dependencies(*lease, document, std::move(declarations), catalog.document(),
                                 &observed);
    prepare_runtime_content_catalog(*lease, roots, inspect_ui, scene.schema());
    std::cout << "UI Material Gallery cooked and inspected with the matching Windows build\n";
#else
    (void)root;
    (void)worker;
    (void)font;
    (void)graph;
    throw std::runtime_error("UI Material Gallery cooking requires the Windows Shader profile");
#endif
}
void verify_export(const std::filesystem::path& root, const std::filesystem::path& worker,
                   const std::filesystem::path& font, bool graph = false) {
    const auto& example = examples[graph ? 1 : 0];
    prepare(root, worker, font, graph);
    const auto catalog = AssetCatalog::open_project(root);
    std::set<AssetId> expected;
    AssetId scene, document;
    for (const auto& [id, record] : catalog.records()) {
        if (record.source == example.scene)
            scene = id;
        if (record.source == example.document)
            document = id;
        if (record.source == example.scene || record.source == example.document ||
            record.source == "Assets/UI/MaterialEffects/Lab.rcss" ||
            record.source == example.materials[0] || record.source == example.materials[1] ||
            record.source == "Assets/Sprites/Badge.png")
            expected.insert(id);
    }
    check(expected.size() == 6 && bool(scene) && bool(document),
          "UI Material Gallery expected source closure differs");
    const auto packaged = root.parent_path() / "content";
    const auto relocated = root.parent_path() / "relocated-content";
    const auto directory = worker.parent_path();
    const auto inspect_ui = [&](AssetId id, std::stop_token stop) {
        return inspect_ui_dependencies(directory / "forge_ui_inspect.exe", font, root, id, stop);
    };
    const std::array roots{scene};
    (void)package_runtime_content(root, packaged, roots, {"windows", "d3d12"}, {}, {}, {},
                                  inspect_ui);
    std::filesystem::rename(root, root.parent_path() / "source-offline");
    std::filesystem::rename(packaged, relocated);
    const auto cooked = open_runtime_content(relocated, {"windows", "d3d12"});
    std::set<AssetId> actual;
    for (const auto& [id, record] : cooked.records()) {
        (void)record;
        actual.insert(id);
    }
    check(actual == expected && !std::filesystem::exists(relocated / example.shader) &&
              !std::filesystem::exists(relocated / example.materials[0]) &&
              !std::filesystem::exists(relocated / example.materials[1]),
          "UI Material Gallery export closure or authored-source removal differs");
    const auto snapshot =
        inspect_ui_dependencies(directory / "forge_ui_inspect.exe", font, relocated, document);
    check(snapshot.documents.contains(document), "Relocated UI Material Gallery did not inspect");
    std::cout << "UI Material Gallery exact six-asset package inspected after source removal and "
                 "relocation\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || argc == 5,
              "Need action, Gallery project, matching worker and optional font");
        const auto root = std::filesystem::absolute(argv[2]);
        const auto worker = std::filesystem::absolute(argv[3]);
        const auto font = argc == 5 ? std::filesystem::absolute(argv[4])
                                    : worker.parent_path() / "resources/ui/LatoLatin-Regular.ttf";
        const auto action = std::string_view(argv[1]);
        if (action == "--author")
            author(root);
        else if (action == "--author-graph")
            author(root, true);
        else if (action == "--inspect") {
            inspect(root);
            inspect(root, true);
        } else if (action == "--verify-export")
            verify_export(root, worker, font);
        else if (action == "--verify-graph-export")
            verify_export(root, worker, font, true);
        else {
            check(action == "--prepare", "Unknown UI Material Gallery action");
            prepare(root, worker, font);
            prepare(root, worker, font, true);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
