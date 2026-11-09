#include "asset_bytes.hpp"
#include "project_text_source.hpp"
#include "runtime_package.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <forge/authoring.hpp>
#include <forge/game_settings.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/project.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/TextShaping.scene.json";
constexpr auto document_path = "Assets/UI/hud.rml";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read_json(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
RuntimeUiInspector inspector(const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [=](AssetId id, std::stop_token) {
        UiTestRenderer renderer;
        const auto settings = ProjectSettings(root).document();
        UiPresenter presenter(renderer, root, font, {}, nullptr,
                              ui_text_shaping_enabled(settings.at("game")),
                              ui_grapheme_editing_enabled(settings.at("game")));
        return presenter.inspect_static({id});
    };
}
void write_sources(const std::filesystem::path& root, const std::filesystem::path& resources,
                   const std::filesystem::path& variable, const std::filesystem::path& color,
                   const std::filesystem::path& foreground) {
    std::filesystem::create_directories(root / "Assets/Fonts");
    std::filesystem::create_directories(root / "Assets/UI");
    std::filesystem::create_directories(root / "Scenes");
    std::filesystem::copy_file(resources / "Cairo-Regular.ttf", root / "Assets/Fonts/Cairo.ttf",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(resources / "LICENSE-Cairo.txt", root / "FONT-LICENSE.txt",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(variable, root / "Assets/Fonts/Weight.ttf",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(color, root / "Assets/Fonts/Color.ttf",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(foreground, root / "Assets/Fonts/Foreground.ttf",
                               std::filesystem::copy_options::overwrite_existing);
    atomic_write(root / document_path, R"rml(<rml><head>
<link type="text/rcss" href="hud.rcss" /></head><body>
<div id="panel"><p id="eyebrow">FORGE / UI TEXT SHAPING</p>
<p id="heading">Letters become glyphs.</p>
<p class="label">Latin ligatures / normal spacing</p><p class="sample">office affinity</p>
<p class="label">Tracked text / separate Latin glyphs</p><p class="sample tracked">office affinity</p>
<p class="label">Combining accents / source text remains UTF-8</p><p class="sample">á é ô</p>
<p class="label">Arabic / an explicit right-to-left run</p><p id="arabic">سلام بالعالم</p>
<p id="effects">Glyph outlines and shadows</p>
<p id="state">Tick {{tick}} / paused {{paused}}</p>
<div id="controls"><button id="Pause" data-event-click="command('Pause')">Pause</button>
<button id="Resume" data-event-click="command('Resume')">Resume</button></div>
<input id="field" type="text" value="office" />
<p id="hint">Shape UI text is enabled in Project Settings. Edit the RML/RCSS in Content,
save and restart Play. This example demonstrates separate horizontal runs, not mixed-direction paragraphs.</p>
<p class="label">Variable weight / original geometric H font</p>
<div id="weights"><div class="weight-card"><p>100</p><p id="weight-light" class="weight-glyph">H</p></div>
<div class="weight-card"><p>400</p><p id="weight-regular" class="weight-glyph">H</p></div>
<div class="weight-card"><p>900</p><p id="weight-heavy" class="weight-glyph">H</p></div></div>
<p class="label">Color layers / original geometric H fonts</p>
<div id="colors"><div class="color-card"><p>Palette</p><p id="color-palette" class="color-glyph">H</p></div>
<div class="color-card"><p>Text color</p><p id="color-foreground" class="color-glyph">H</p></div>
<div class="color-card"><p>50% + fx</p><p id="color-effects" class="color-glyph">H</p></div></div>
<p class="label">Palette colors retain their own hue. Foreground layers use text color; text alpha fades the whole glyph.</p>
</div></body></rml>)rml");
    atomic_write(root / "Assets/UI/hud.rcss",
                 R"rcss(@font-face {font-family:ForgeArabic;src:"Assets/Fonts/Cairo.ttf";}
@font-face {font-family:ForgeWeight;src:"Assets/Fonts/Weight.ttf";}
@font-face {font-family:ForgeColor;src:"Assets/Fonts/Color.ttf";}
@font-face {font-family:ForgeForeground;src:"Assets/Fonts/Foreground.ttf";}
body {width:100%;height:100%;font-family:Lato;font-size:18px;color:#e7eef7;pointer-events:none;}
body,div,p {display:block;} p {margin:8px 0;}
#panel {position:absolute;left:5%;top:4%;width:90%;max-width:740px;max-height:92%;overflow:auto;box-sizing:border-box;padding:24px;background-color:#152b3df5;pointer-events:auto;}
#eyebrow,.label {color:#8cd8cc;font-size:14px;} #heading {font-size:30px;color:#ffffff;}
.sample,#arabic {font-size:32px;margin-bottom:14px;} .tracked {letter-spacing:2px;}
#arabic {font-family:ForgeArabic;-rmlui-language:ar;-rmlui-direction:rtl;}
#effects {font-size:26px;font-effect:shadow(2px 2px #0008), outline(1px #5c9379);}
#state {font-size:14px;color:#bdd0e3;} button {padding:8px 16px;margin:4px;background-color:#315a72;}
#controls {height:48px;}
input {display:block;box-sizing:border-box;width:90%;height:40px;padding:6px;margin-top:4px;background-color:#0a1d2b;color:#ffffff;}
#hint {font-size:14px;color:#c0cfdd;}
#weights {height:120px;} .weight-card {display:inline-block;width:32%;vertical-align:top;}
.weight-glyph {font-family:ForgeWeight;font-size:64px;}
#weight-light {font-weight:100;} #weight-regular {font-weight:400;} #weight-heavy {font-weight:900;}
#colors {height:150px;} .color-card {display:inline-block;width:32%;vertical-align:top;}
.color-card p {font-size:14px;} .color-card .color-glyph {font-family:ForgeColor;font-size:64px;}
#color-foreground {font-family:ForgeForeground;color:#7be4d5;}
#color-effects {font-family:ForgeForeground;color:#7be4d580;font-effect:shadow(2px 2px #0008), outline(1px #fff);}
scrollbarvertical {width:12px;background-color:#101e2c;} scrollbarvertical sliderbar {background-color:#477e9a;}
)rcss");
}
void author(const std::filesystem::path& root, const std::filesystem::path& resources,
            const std::filesystem::path& variable, const std::filesystem::path& color,
            const std::filesystem::path& foreground) {
    check(!std::filesystem::exists(root), "Choose a new path; never reauthor a published example");
    write_sources(root, resources, variable, color, foreground);
    const auto hud = register_ui_document(root, document_path);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name) {
        auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                       .at("selected")
                                       .get<std::string>());
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    create("Text shaping HUD").set<UiDocument>({{hud.id}, true, true, 0});
    auto camera = create("Game Camera");
    camera.set<Camera>({});
    camera.set<LocalTranslation>({0, 0, -8});
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    ProjectSettings settings(root);
    auto candidate = settings.document();
    candidate["name"] = "UI Text Shaping";
    candidate["startup_scene"] = {{"asset", record.id}, {"source", scene_path}};
    candidate["game"] = default_game_settings("org.forge.examples.text-shaping", "UI Text Shaping");
    candidate["game"]["display"]["height"] = 800;
    candidate["game"]["ui"]["text_shaping"] = true;
    candidate["game"]["ui"]["grapheme_editing"] = true;
    settings.save(std::move(candidate));
    ProjectLease lease(root);
    const std::array roots{record.id};
    prepare_runtime_content_catalog(
        lease, roots,
        inspector(root,
                  asset_detail::read_bytes(resources / "LatoLatin-Regular.ttf", 4 * 1024 * 1024)),
        scene.schema());
}
void exercise(const std::filesystem::path& root, const Json& source,
              const std::vector<std::byte>& font, const Json& settings, bool reload) {
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font, {}, nullptr,
                              ui_text_shaping_enabled(settings.at("game")),
                              ui_grapheme_editing_enabled(settings.at("game")));
        presenter.reset("shaping-project", 1);
        const bool accepted = presenter.accept(ui->snapshot(scene, "shaping-project", 1, 41, true));
        check(accepted, presenter.diagnostic());
        presenter.update(0, 900, 800);
        presenter.render();
        check(presenter.diagnostic().empty(), presenter.diagnostic());
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* text = document->GetElementById("arabic");
        check(text && text->GetNumChildren() == 1 &&
                  static_cast<Rml::ElementText*>(text->GetChild(0))->GetText() == "سلام بالعالم",
              "Shaping changed the source text or its native DOM");
        check(document->GetElementById("state")->GetInnerRML() == "Tick 41 / paused 1",
              "Shaped project lost its native model binding");
        check(renderer.draws && !renderer.textures.empty() && presenter.asset_snapshot() &&
                  presenter.asset_snapshot()->sources.size() == 6,
              "Shaped project did not admit its RML, stylesheet and four project fonts");
        check(document->GetElementById("weight-light") && document->GetElementById("weight-heavy"),
              "Variable weight comparison is missing from the saved/relocated UI");
        check(ui_grapheme_editing_enabled(settings.at("game")),
              "Example does not enable whole-character editing");
        auto* field =
            rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("field"));
        check(field && field->Focus(), "Example text field did not focus");
        field->SetValue("a\xcc\x81"
                        "b");
        field->SetSelectionRange(2, 2);
        presenter.key(Rml::Input::KI_BACK, true, 0);
        presenter.key(Rml::Input::KI_BACK, false, 0);
        check(field->GetValue() == "b", "Saved/relocated example split a combining character");
        presenter.release_input();
        const auto position = document->GetElementById("Resume")->GetAbsoluteOffset();
        presenter.mouse_move(int(position.x + 4), int(position.y + 4), 0);
        presenter.mouse_button(0, true, 0);
        presenter.mouse_button(0, false, 0);
        auto command = presenter.pending_command();
        check(command.has_value(), "Shaped native project button did not enqueue its command");
        std::string operation;
        ui->command(scene, *command, [&](const std::string& name) { operation = name; });
        check(operation == "Resume", "Shaped native command did not reach the runtime owner");
        if (reload) {
            const auto path = root / document_path;
            const auto saved = ProjectTextSource::read(path);
            check(saved.has_value(), "Shaped project source disappeared");
            atomic_write(path, "<rml><body><script>invalid</script></body></rml>");
            check(!presenter.reload() && presenter.document_count() == 1,
                  "Failed native reload discarded the shaped last-good context");
            atomic_write(path, *saved);
            check(presenter.reload(), presenter.diagnostic());
            presenter.update(.1, 900, 800);
            presenter.render();
        }
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Shaped project retained glyph resources after shutdown");
    check(scene.snapshot() == source, "Shaping or UI controls modified the authored Scene");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 2, "Need an example mode or project path");
        const auto mode = std::string_view(argv[1]);
        if (mode == "--author" || mode == "--refresh") {
            check(argc == 7,
                  "Need mode, project, resources, original variable, palette and foreground fonts");
            const auto root = std::filesystem::absolute(argv[2]);
            if (mode == "--author")
                author(root, argv[3], argv[4], argv[5], argv[6]);
            else {
                check(std::filesystem::is_regular_file(root / scene_path), "Example Scene missing");
                const auto catalog = AssetCatalog::open_project(root);
                write_sources(root, argv[3], argv[4], argv[5], argv[6]);
                ProjectLease lease(root);
                const std::array roots{read_json(root / scene_path).at("asset_id").get<AssetId>()};
                prepare_runtime_content_catalog(
                    lease, roots,
                    inspector(root, asset_detail::read_bytes(std::filesystem::path(argv[3]) /
                                                                 "LatoLatin-Regular.ttf",
                                                             4 * 1024 * 1024)));
                const auto refreshed = AssetCatalog::open_project(root);
                for (const auto& [id, record] : catalog.records())
                    check(refreshed.records().at(id).source == record.source,
                          "Refresh replaced an existing example asset identity");
            }
            return 0;
        }
        check(argc == 4, "Need project, scratch and UI resource directory");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto settings = ProjectSettings(root).document();
        const auto source = read_json(root / scene_path);
        const auto font = asset_detail::read_bytes(
            std::filesystem::path(argv[3]) / "LatoLatin-Regular.ttf", 4 * 1024 * 1024);
        exercise(root, source, font, settings, true);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            ProjectLease lease(root);
            prepare_runtime_content_catalog(lease, roots, inspector(root, font));
        }
        package_runtime_content(root, scratch / "export", roots, {"windows", "d3d12"}, {}, {}, {},
                                inspector(root, font));
        const auto moved = scratch / "relocated";
        std::filesystem::rename(scratch / "export", moved);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        check(catalog.records().size() == 7,
              "Shaped runtime closure lost or duplicated its seven assets");
        std::filesystem::rename(root, scratch / "source-offline");
        exercise(moved, read_json(moved / catalog.records().at(roots[0]).source), font, settings,
                 false);
        std::cout << "Saved opt-in UI project, native text/styles/input/last-good resources and "
                     "source-hidden relocated font content passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
