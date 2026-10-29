#include "editor/source_import.hpp"
#include "reflection_capture_source.hpp"
#include "texture_authoring.hpp"
#include <backends/imgui_impl_sdlrenderer3.h>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& worker, const std::filesystem::path& scratch,
              float scale) {
    const auto root = scratch / AssetId::generate().str();
    SceneDocument::create_project(root, "Atlas packing");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto fixtures = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "samples/projects/FeatureGallery/Assets/Sprites";
    const auto registry = texture_import_registry(worker);
    const AssetReimportRoute route{"forge.texture.image", desktop_texture_target(), registry,
                                   [](auto& candidate, const auto& plan, const auto&) {
                                       prepare_texture_publication(candidate, plan);
                                   }};
    std::vector<AssetId> inputs;
    {
        AssetImportService initial(project.writer_guard(), registry, route.target, 1);
        for (const auto* name : {"Badge.png", "Panel.png"}) {
            std::filesystem::copy_file(fixtures / name, root / "Assets" / name);
            const auto job = initial.submit(
                initial.prepare(std::filesystem::path("Assets") / name, route.importer),
                route.prepare, [](const auto&, const auto&) {});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            bool done = false;
            while (!done) {
                check(std::chrono::steady_clock::now() < deadline, "Input import timed out");
                for (const auto& outcome : initial.poll())
                    if (outcome.job.id == job) {
                        check(outcome.published, outcome.diagnostic);
                        inputs.push_back(outcome.job.asset);
                        done = true;
                    }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(int(1080 * scale), int(800 * scale), SDL_PIXELFORMAT_RGBA32),
        SDL_DestroySurface);
    check(bool(surface), SDL_GetError());
    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    check(bool(renderer), SDL_GetError());
    const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), ImGui::DestroyContext);
    check(ImGui_ImplSDLRenderer3_Init(renderer.get()), "Native ImGui software backend unavailable");
    struct Backend {
        ~Backend() { ImGui_ImplSDLRenderer3_Shutdown(); }
    } backend;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1080 * scale, 800 * scale};
    io.DeltaTime = 1.f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigInputTrickleEventQueue = false;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16) != nullptr,
          "Missing actual UI font");
    ui::style(scale);
    ui::EditorUiContext ui_context;
    ui::ContextScope scope(ui_context);
    test::observe_ui = true;
    SourceImport source;
    source.atlas_worker = worker;
    source.routes = [route] {
        auto container = route;
        container.importer = "forge.texture.container";
        return std::vector{route, container};
    };
    ContentImports imports;
    imports.routes = source.routes;
    std::string message;
    imports.poll(project, message);
    source.atlas(project, inputs);
    const auto authored = scene.document();
    bool locked = false;
    auto frame = [&] {
        imports.suspend(source.busy());
        source.poll(project, imports, message);
        imports.poll(project, message);
        test::ui_targets.clear();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("FORGE Content", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::TextUnformatted("Published image textures: Badge.png, Panel.png");
        source.draw(locked);
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer.get(), 12, 14, 18, 255);
        SDL_RenderClear(renderer.get());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        SDL_RenderPresent(renderer.get());
    };
    const auto click = [&](const std::string& id) {
        frame();
        const auto found = test::ui_targets.find(id);
        check(found != test::ui_targets.end(), "Missing atlas control: " + id);
        const auto target = found->second;
        check(target.enabled && target.minimum.x >= target.clip_minimum.x &&
                  target.maximum.x <= target.clip_maximum.x &&
                  target.minimum.y >= target.clip_minimum.y &&
                  target.maximum.y <= target.clip_maximum.y,
              "Atlas control clipped or disabled: " + id);
        io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                            (target.minimum.y + target.maximum.y) * .5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    const auto wait = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!predicate()) {
            frame();
            check(std::chrono::steady_clock::now() < deadline,
                  source.operation().dump() + " Atlas UI timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        frame();
    };
    frame();
    frame();
    click("atlas:trim");
    click("atlas:rotation");
    if (scale == 1)
        check(SDL_SavePNG(surface.get(), (scratch / "sprite-atlas-options.png").string().c_str()),
              SDL_GetError());
    click("button:Prepare import");
    wait([&] { return source.review_ready(); });
    const auto first = source.operation();
    check(first.at("generated").at("inputs")[0].at("asset") == inputs[0].str() &&
              first.at("generated").at("settings").at("trim_transparent") == true &&
              first.at("generated").at("settings").at("allow_rotation") == true &&
              first.at("generated").at("frames").size() == 2 &&
              !std::filesystem::exists(root / "Assets/Atlas"),
          "Atlas review mutated files or lost input order");
    check(test::ui_targets.contains("atlas:layout"), "Review omitted the real layout");
    if (scale == 1)
        check(SDL_SavePNG(surface.get(), (scratch / "sprite-atlas-review.png").string().c_str()),
              SDL_GetError());
    locked = true;
    frame();
    check(!test::ui_targets.at("button:Copy sources").enabled,
          "Locked atlas allowed source publication");
    locked = false;
    click("button:Back");
    click("button:Prepare import");
    wait([&] { return source.review_ready(); });
    check(source.operation().at("review") == first.at("review"),
          "Unchanged input review is not deterministic");
    click("button:Copy sources");
    wait([&] { return source.finished(); });
    check(source.published_count() == 1 && scene.document() == authored,
          "Atlas UI failed publication or changed authored Scene/history");
    const auto outcome = source.operation();
    const auto id = AssetId::parse(outcome.at("results")[0].at("asset").get<std::string>());
    check(AssetCatalog::open_project(root).records().at(id).source == "Assets/Atlas/atlas.png",
          "Atlas UI did not use ordinary texture source publication");
    click("button:Copy atlas frames");
    const auto clipboard = Json::parse(ImGui::GetClipboardText());
    check(clipboard.at("texture") == id.str() &&
              clipboard.at("frames") == outcome.at("generated").at("frames") &&
              clipboard.size() == 6,
          "Published atlas clipboard lost its Texture identity or layout");
    click("button:Close");
    check(!source.busy(), "Closed atlas retained its operation lock");
    source.atlas(project, inputs);
    source.prepare();
    source.automation_cancel(source.operation().at("batch"));
    wait([&] { return source.operation().value("state", std::string{}) == "cancelled"; });
    check(!std::filesystem::exists(root / "Assets/Atlas2"),
          "Cancelled preparation copied a source");
    source.automation_close(source.operation().at("batch"));
    check(scene.document() == authored, "Cancelled atlas changed the Scene");
    // Generated HDR capture bytes use the same review/copy/import owner as the
    // atlas. They allocate no identity until the existing import is committed.
    TextureData cube;
    cube.width = cube.height = 2;
    cube.dimension = TextureDimension::Cube;
    cube.format = TextureFormat::RGBA16Float;
    cube.semantic = TextureSemantic::HdrColor;
    cube.alpha = TextureAlpha::Opaque;
    cube.subresources.resize(6, std::vector<std::byte>(32));
    for (auto& face : cube.subresources)
        for (std::size_t at = 0; at < face.size(); at += 2) {
            const unsigned bits = at % 8 == 6 ? 0x3c00 : 0x4000;
            face[at] = std::byte(bits & 255);
            face[at + 1] = std::byte(bits >> 8);
        }
    const auto before_generated = AssetCatalog::open_project(root);
    std::ifstream index(root / "forge.assets.json", std::ios::binary);
    const std::string catalog_bytes{std::istreambuf_iterator<char>(index), {}};
    const auto catalog_digest =
        asset_detail::content_digest(std::as_bytes(std::span(catalog_bytes)));
    source.generated_source(project, "Reflection.dds", encode_reflection_capture_source(cube),
                            {{"kind", "reflection_map_capture"}, {"size", 2}}, catalog_digest,
                            "Assets/GeneratedReflection", false);
    wait([&] { return source.review_ready(); });
    const auto generated_review = source.operation();
    check(generated_review.at("generated").at("kind") == "reflection_map_capture" &&
              AssetCatalog::open_project(root).records().size() ==
                  before_generated.records().size() &&
              !std::filesystem::exists(root / "Assets/GeneratedReflection"),
          "Generated reflection review changed files or asset identities");
    source.automation_commit(generated_review.at("batch"), generated_review.at("review"), true);
    wait([&] { return source.finished(); });
    const auto generated_result = source.operation();
    check(source.published_count() == 1 && scene.document() == authored,
          "Generated reflection failed normal import or changed the Scene");
    const auto generated_id =
        AssetId::parse(generated_result.at("results")[0].at("asset").get<std::string>());
    const auto after_generated = AssetCatalog::open_project(root);
    check(after_generated.records().at(generated_id).source ==
                  "Assets/GeneratedReflection/Reflection.dds" &&
              after_generated.records().size() == before_generated.records().size() + 1,
          "Generated reflection did not use one ordinary Texture asset identity");
    const auto old_records = before_generated.document().at("assets");
    const auto new_records = after_generated.document().at("assets");
    for (const auto& record : old_records)
        check(std::find(new_records.begin(), new_records.end(), record) != new_records.end(),
              "Generated reflection changed a prior asset record");
    source.automation_close(generated_result.at("batch"));
    frame();
    check(!source.busy(), "Generated source close retained the import lock");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need asset worker and scratch directory");
        const auto scratch = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(scratch);
        for (const auto scale : {1.f, 1.5f, 2.f})
            exercise(std::filesystem::absolute(argv[1]), scratch, scale);
        std::cout << "Actual atlas review, locked controls, publication, clipboard and "
                     "cancellation at three UI scales passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
