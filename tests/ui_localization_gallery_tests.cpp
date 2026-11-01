#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include "ui_localization_fixture.hpp"
#include "ui_test_renderer.hpp"
#include <forge/authoring.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/133-ui-localization.scene.json";
constexpr const char* table_path = "Assets/UI/Localization/Messages.localization.json";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
RuntimeUiInspector inspector(const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [=](AssetId id, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, root, font);
        return presenter.inspect_static({id});
    };
}
void prepare(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    const auto directory = root / "Assets/UI/Localization";
    std::filesystem::create_directories(directory);
    atomic_write(directory / "hud.rml", R"rml(<rml><head>
<link type="text/rcss" href="hud.rcss" />
</head><body><div id="panel">
<p id="eyebrow">FORGE / GAME UI</p>
<h1 id="title">{{ 'title' | tr(forge_locale) }}</h1>
<p id="intro">{{ 'intro' | tr(forge_locale) }}</p>
<div class="languages">
<button id="english" data-event-click="command('forge.locale', 'en')">English</button>
<button id="french" data-event-click="command('forge.locale', 'fr')">Français</button>
<button id="spanish" data-event-click="command('forge.locale', 'es')">Español</button>
</div>
<div id="preview"><h2 id="play-label">{{ 'menu_play' | tr(forge_locale) }}</h2>
<p id="literal">{{ 'literal' | tr(forge_locale) }}</p>
<p id="fallback">{{ 'fallback_note' | tr(forge_locale) }}</p></div>
<p>Requested: {{forge_locale}} / Selected: {{forge_resolved_locale}}</p>
<button id="regional" data-event-click="command('forge.locale', 'fr-CA')">fr-CA: primary fallback</button>
<button id="unsupported" data-event-click="command('forge.locale', 'de')">de: default fallback</button>
<div class="rule"></div><p>Paused: {{paused}} / Tick: {{tick}}</p>
<button id="pause" data-event-click="command('Pause')">Pause</button>
<button id="resume" data-event-click="command('Resume')">Resume</button>
<p class="small">Language controls stay active while paused. Stop restores the authored Locale.</p>
</div></body></rml>)rml");
    atomic_write(directory / "hud.rcss", R"rcss(body, div, h1, h2, p {display:block;}
body {width:100%;height:100%;font-family:Lato;font-size:16px;color:#e9f2f4;pointer-events:none;}
#panel {position:absolute;left:5%;top:4%;width:90%;max-width:740px;max-height:92%;box-sizing:border-box;padding:22px;background-color:#142a38;overflow:auto;pointer-events:auto;}
#eyebrow {color:#65d6bb;font-size:13px;margin:0 0 10px;}
h1 {font-size:30px;margin:0 0 10px;} h2 {font-size:24px;color:#65d6bb;margin:0 0 10px;}
p {margin:10px 0;line-height:1.4;} #intro {color:#b9d1db;}
button {display:inline-block;box-sizing:border-box;padding:10px 12px;margin:4px 6px 4px 0;background-color:#294b60;}
button:hover,button:focus {background-color:#40728a;}
#preview {padding:16px;background-color:#0c1c27;margin:14px 0;}
#literal {color:#e9bf84;} #fallback {color:#b9d1db;}
.rule {height:1px;background-color:#315060;margin:14px 0;}
.small {font-size:14px;color:#b9d1db;}
scrollbarvertical {width:10px;background-color:#0c1c27;}
scrollbarvertical sliderbar {background-color:#40728a;})rcss");
    auto lease = std::make_shared<ProjectLease>(root);
    AssetId table;
    if (std::filesystem::exists(root / table_path))
        table = UiLocalizationDocument(lease, table_path).source().asset();
    else
        table = test_ui_localization::publish(lease, table_path, [](AssetId id) {
            auto source = UiLocalizationSource::create(id);
            source.document["locales"] = {
                {"en",
                 {{"label", "English"},
                  {"messages",
                   {{"title", "Language lab"},
                    {"intro", "Choose a language. This menu updates without restarting."},
                    {"menu_play", "Start playing"},
                    {"literal", "Crème & café <b>literal</b> {{text}}"},
                    {"fallback_note",
                     "This note uses the default language when a translation is missing."}}}}},
                {"fr",
                 {{"label", "Français"},
                  {"messages",
                   {{"title", "Atelier des langues"},
                    {"intro", "Choisissez une langue. Le menu change sans redémarrer."},
                    {"menu_play", "Jouer maintenant"},
                    {"literal", "Crème & café <b>texte</b> {{texte}}"}}}}},
                {"es",
                 {{"label", "Español"},
                  {"messages",
                   {{"title", "Laboratorio de idiomas"},
                    {"intro", "Elige un idioma. El menú cambia sin reiniciar."},
                    {"menu_play", "Empezar a jugar"},
                    {"literal", "Café & té <b>literal</b> {{texto}}"}}}}}};
            return source;
        });
    const auto hud = register_ui_document(root, "Assets/UI/Localization/hud.rml");
    EngineContext author;
    Scene scene(author.world());
    if (std::filesystem::exists(root / scene_path))
        scene.load(root / scene_path);
    else {
        auto create = [&](const char* name) {
            auto e = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                      .at("selected")
                                      .get<std::string>());
            e.set<Primitive>({no_primitive});
            return e;
        };
        create("Language lab HUD").set<UiDocument>({{hud.id}, true, true, 0});
        create("Language controller - inspect Locale").set<UiLocalization>({{table}, "en"});
        auto camera = create("Game Camera");
        camera.set<Camera>({});
        camera.set<LocalTranslation>({0, 0, -8});
    }
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, inspector(root, font), scene.schema());
}
void exercise(const std::filesystem::path& root, const Json& source,
              const std::vector<std::byte>& font) {
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    const auto state = ui->snapshot(scene, "gallery-localization", 1, 0, true);
    check(state.at("documents").size() == 1 && state.contains("localization"),
          "Gallery localization Scene lost its controller or HUD");
    UiTestRenderer renderer;
    UiPresenter presenter(renderer, root, font);
    presenter.reset("gallery-localization", 1);
    check(presenter.accept(state), presenter.diagnostic());
    presenter.update(.1, 800, 720);
    presenter.render();
    check(renderer.draws > 0 && presenter.localization_state().at("selected_locale") == "en",
          "Gallery did not produce an admitted localized native UI");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || argc == 5,
              "Need Gallery, scratch directory, font and optional --prepare");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        if (argc == 5) {
            check(std::string_view(argv[4]) == "--prepare", "Unknown Gallery preparation option");
            prepare(root, font);
            std::cout << path_utf8(root) << '\n';
            return 0;
        }
        const auto source = read(root / scene_path);
        exercise(root, source, font);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        const auto exported = scratch / "export";
        package_runtime_content(root, exported, roots, {"windows", "d3d12"}, {}, {}, {},
                                inspector(root, font));
        const auto moved = scratch / "Relocated game";
        std::filesystem::rename(exported, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 4,
              "Gallery localized Scene/RML/style/table closure changed");
        check(!std::filesystem::exists(moved / table_path),
              "Export copied editable localization source");
        check(read(root / scene_path) == source, "Gallery export changed authored Scene data");
        std::filesystem::rename(root, scratch / "Sources offline");
        exercise(moved, read(moved / shipped.records().at(roots[0]).source), font);
        std::filesystem::remove_all(scratch);
        std::cout << "Gallery localization native UI and relocated source-free closure passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
