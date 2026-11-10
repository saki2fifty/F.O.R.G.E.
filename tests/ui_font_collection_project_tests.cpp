#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "fixtures/ui_font_collection.hpp"
#include "runtime_package.hpp"
#include "ui_asset_catalog.hpp"
#include "ui_markup_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/game_settings.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/project.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace forge;
constexpr auto scene_path = "Scenes/FontCollections.scene.json";
constexpr auto hud_path = "Assets/UI/hud.rml";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
RuntimeUiInspector inspect(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    return [=](AssetId asset, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, root, font, {}, nullptr, true);
        return presenter.inspect_static({asset});
    };
}
void author(const std::filesystem::path& root, const std::filesystem::path& fonts) {
    check(!std::filesystem::exists(root),
          "Choose a new example path; never reauthor published IDs");
    std::filesystem::create_directories(root / "Assets/Fonts");
    std::filesystem::create_directories(root / "Assets/UI");
    std::filesystem::create_directories(root / "Scenes");
    const auto latin = asset_detail::read_bytes(fonts / "Poppins-Regular.ttf", 4 * 1024 * 1024);
    const auto arabic = asset_detail::read_bytes(fonts / "Cairo-Regular.ttf", 4 * 1024 * 1024);
    const auto collection = test::font_collection({latin, arabic});
    std::ofstream output(root / "Assets/Fonts/Menu.ttc", std::ios::binary);
    output.write(reinterpret_cast<const char*>(collection.data()),
                 std::streamsize(collection.size()));
    output.close();
    check(bool(output), "Could not write example font container");
    for (const auto* name : {"LICENSE-Poppins.txt", "LICENSE-Cairo.txt"})
        std::filesystem::copy_file(fonts / name, root / name);
    std::ofstream(root / hud_path) << R"rml(<rml><head>
<link type="text/rcss" href="hud.rcss" /></head><body>
<div id="panel"><p id="eyebrow">FORGE / FONT COLLECTIONS</p>
<p id="heading">One file. Two font faces.</p>
<p class="label">Face 0 / Poppins</p><p id="latin">The quick brown fox / 0123456789</p>
<p class="label">Face 1 / Cairo</p><p id="second">The quick brown fox / 0123456789</p>
<p id="arabic">سلام بالعالم</p>
<p class="label">A native field using face 1</p><input id="field" type="text" value="Edit this text" />
<p id="state">Tick {{tick}} / paused {{paused}}</p>
<p id="hint">Open Assets/UI/hud.rcss in Content. Each font-family selects a zero-based face
from Assets/Fonts/Menu.ttc. Save changes and restart Play. Shape UI text is enabled.</p>
</div></body></rml>)rml";
    std::ofstream(root / "Assets/UI/hud.rcss")
        << R"rcss(@font-face {font-family:MenuLatin;src:"Assets/Fonts/Menu.ttc";-rmlui-face-index:0;}
@font-face {font-family:MenuArabic;src:"Assets/Fonts/Menu.ttc";-rmlui-face-index:1;}
body {width:100%;height:100%;font-family:Lato;font-size:18px;color:#edf3fa;pointer-events:none;}
body,div,p {display:block;} p {margin:8px 0;}
#panel {position:absolute;left:5%;top:4%;width:90%;max-width:800px;max-height:92%;overflow:auto;box-sizing:border-box;padding:24px;background-color:#152b3df5;pointer-events:auto;}
#eyebrow,.label {font-size:14px;color:#8cd8cc;} #heading {font-size:30px;color:#fff;}
#latin {font-family:MenuLatin;font-size:28px;} #second,#arabic {font-family:MenuArabic;font-size:28px;}
#arabic {-rmlui-language:ar;-rmlui-direction:rtl;}
input {display:block;box-sizing:border-box;width:90%;height:48px;padding:6px;background-color:#0a1d2b;color:#fff;font-family:MenuArabic;}
#state,#hint {font-size:14px;color:#bdd0e3;} scrollbarvertical {width:12px;background-color:#101e2c;}
scrollbarvertical sliderbar {background-color:#477e9a;}
)rcss";
    const auto hud = register_ui_document(root, hud_path);
    EngineContext engine;
    Scene scene(engine.world());
    const auto entity = [&](const char* name) {
        const auto id = authoring_command(scene, "entity.create", {{"name", name}}).at("selected");
        auto value = scene.entity(id.get<std::string>());
        value.set<Primitive>({no_primitive});
        return value;
    };
    entity("Font collection HUD").set<UiDocument>({{hud.id}, true, true, 0});
    entity("Game Camera").set<Camera>({});
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto saved_scene = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    ProjectSettings settings(root);
    auto value = settings.document();
    value["name"] = "UI Font Collections";
    value["startup_scene"] = {{"asset", saved_scene.id}, {"source", scene_path}};
    value["game"] =
        default_game_settings("org.forge.examples.font-collections", "UI Font Collections");
    value["game"]["ui"] = {{"text_shaping", true}, {"grapheme_editing", true}};
    settings.save(value);
    ProjectLease lease(root);
    const std::array roots{saved_scene.id};
    prepare_runtime_content_catalog(
        lease, roots,
        inspect(root, asset_detail::read_bytes(fonts / "LatoLatin-Regular.ttf", 4 * 1024 * 1024)),
        scene.schema());
}
AssetId author_font_selection(const std::filesystem::path& root,
                              const std::vector<std::byte>& font) {
    ProjectLease lease(root);
    const auto catalog = AssetCatalog::open_project(root);
    const auto selected =
        std::find_if(catalog.records().begin(), catalog.records().end(),
                     [](const auto& row) { return row.second.type == "ui_font"; });
    const auto document =
        std::find_if(catalog.records().begin(), catalog.records().end(),
                     [](const auto& row) { return row.second.source == hud_path; });
    check(selected != catalog.records().end() && document != catalog.records().end(),
          "Font authoring fixture lacks its registered resources");
    const auto reference = ui_font_reference(catalog, selected->first, 1);
    const auto path = root / "Assets/UI/hud.rcss";
    auto text = *asset_storage::read(path);
    text = insert_ui_stylesheet_rule(text, "#preset-center");
    text = set_ui_stylesheet_property(text, inspect_ui_stylesheet(text).size() - 1,
                                      "background-color", "#117733");
    text = set_ui_stylesheet_media_query(
        text, {},
        {UiStyleNumber{800, UiStyleUnit::Pixels}, UiStyleNumber{1100, UiStyleUnit::Pixels}});
    text = set_ui_stylesheet_media_rule(text, inspect_ui_stylesheet(text).size() - 1, {},
                                        "#preset-center", {{"background-color", "#3355bb"}});
    text = set_ui_stylesheet_font_face(text, {}, "AuthoredCollectionFace",
                                       reference.at("source").get<std::string>(), 0);
    text = set_ui_stylesheet_font_face_style(text, inspect_ui_stylesheet(text).size() - 1,
                                             {{"font-weight", "400"}, {"font-style", "normal"}});
    text = set_ui_stylesheet_font_face(text, {}, "AuthoredCollectionFace",
                                       reference.at("source").get<std::string>(), 1);
    text = set_ui_stylesheet_font_face_style(text, inspect_ui_stylesheet(text).size() - 1,
                                             {{"font-weight", "700"}, {"font-style", "italic"}});
    const auto rules = inspect_ui_stylesheet(text);
    const auto input = std::find_if(rules.begin(), rules.end(), [&](const auto& rule) {
        return rule.flat &&
               std::string_view(text)
                   .substr(rule.selector.begin, rule.selector.end - rule.selector.begin)
                   .starts_with("input ");
    });
    check(input != rules.end(), "Font authoring fixture lacks its input selector");
    text = set_ui_stylesheet_property(text, std::size_t(input - rules.begin()), "font-family",
                                      "AuthoredCollectionFace");
    text =
        set_ui_stylesheet_property(text, std::size_t(input - rules.begin()), "font-weight", "700");
    text = set_ui_stylesheet_property(text, std::size_t(input - rules.begin()), "font-style",
                                      "italic");
    asset_storage::replace(path, text);
    auto markup = *asset_storage::read(root / hud_path);
    const auto nodes = inspect_ui_markup(markup);
    const auto head = std::find_if(nodes.begin(), nodes.end(),
                                   [](const auto& node) { return node.tag == "head"; });
    check(head != nodes.end(), "Native stylesheet fixture lacks its original head");
    markup.erase(head->source.begin, head->source.end - head->source.begin);
    const auto stylesheet =
        std::find_if(catalog.records().begin(), catalog.records().end(),
                     [](const auto& row) { return row.second.source == "Assets/UI/hud.rcss"; });
    check(stylesheet != catalog.records().end(), "Original stylesheet has no asset identity");
    const auto original_link = ui_stylesheet_reference(catalog, hud_path, stylesheet->first);
    markup =
        set_ui_markup_stylesheet_link(markup, {}, original_link.at("reference").get<std::string>());
    std::filesystem::create_directories(root / "Assets/Themes");
    for (const auto& [name, color] :
         {std::pair{"base", "#117733"}, std::pair{"override", "#3355bb"}}) {
        const auto source = std::filesystem::path("Assets/Themes") / (std::string(name) + ".rcss");
        asset_storage::replace(root / source, std::string("#field{color:") + color + ";}");
        const auto asset = register_ui_source(lease, source);
        const auto link =
            ui_stylesheet_reference(AssetCatalog::open_project(root), hud_path, asset.id);
        markup = set_ui_markup_stylesheet_link(markup, {}, link.at("reference").get<std::string>());
    }
    for (const auto& [preset, id] :
         {std::pair{"center", "preset-center"}, std::pair{"stretch_all", "preset-stretch"}}) {
        const auto current = inspect_ui_markup(markup);
        const auto body =
            std::ranges::find(current, std::string_view("body"), &UiMarkupElement::tag);
        check(body != current.end(), "Layout export fixture lacks a body");
        const auto added = insert_ui_markup_control(markup, std::size_t(body - current.begin()),
                                                    "container", {}, {}, id);
        markup = set_ui_markup_layout_preset(added.source, added.element, preset, 120, 40, 8, 6);
    }
    asset_storage::replace(root / hud_path, markup);
    refresh_ui_asset_catalog(lease, inspect(root, font)(document->first, {}));
    return document->first;
}
void present(const std::filesystem::path& root, const std::vector<std::byte>& font,
             const std::filesystem::path& scene_source = scene_path, bool shaped = true,
             bool fallback = false, bool overlay_last = true) {
    Json saved;
    std::ifstream(root / scene_source) >> saved;
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(saved);
    const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font, {}, nullptr, shaped, true);
        presenter.reset("font-collection-project", 1);
        const auto snapshot = ui->snapshot(scene, "font-collection-project", 1, 7, true);
        check(presenter.accept(snapshot), presenter.diagnostic().c_str());
        presenter.update(0, 1000, 720);
        presenter.render();
        check(renderer.draws && !renderer.textures.empty() && presenter.diagnostic().empty(),
              "Collection example native rendering failed");
        const auto fields = presenter.text_fields(snapshot.at("documents")[0].at("instance"));
        check(!fields.at("fields").empty(), "Collection example text field missing");
        auto* input = Rml::GetContext(0)->GetDocument(0)->GetElementById("field");
        auto* fonts = Rml::GetFontEngineInterface();
        const auto selected =
            fonts->GetFontFaceHandle("authoredcollectionface", Rml::Style::FontStyle::Italic,
                                     Rml::Style::FontWeight::Bold, 32);
        const auto regular =
            fonts->GetFontFaceHandle("authoredcollectionface", Rml::Style::FontStyle::Normal,
                                     Rml::Style::FontWeight::Normal, 32);
        const Rml::String language = "en";
        const Rml::TextShapingContext context{language};
        const auto width = [&](Rml::FontFaceHandle handle) {
            check(bool(handle), "Authored font face did not load");
            return fonts->GetStringWidth(handle, "office", context);
        };
        check(input &&
                  Rml::StringUtilities::ToLower(input->GetProperty<Rml::String>("font-family")) ==
                      "authoredcollectionface" &&
                  input->GetProperty<Rml::Style::FontStyle>("font-style") ==
                      Rml::Style::FontStyle::Italic &&
                  input->GetProperty<Rml::Style::FontWeight>("font-weight") ==
                      Rml::Style::FontWeight::Bold &&
                  input->GetFontFaceHandle() ==
                      fonts->GetFontFaceHandle("authoredcollectionface",
                                               Rml::Style::FontStyle::Italic,
                                               Rml::Style::FontWeight::Bold, 18) &&
                  selected && regular && selected != regular &&
                  width(selected) ==
                      width(fonts->GetFontFaceHandle("menuarabic", Rml::Style::FontStyle::Normal,
                                                     Rml::Style::FontWeight::Normal, 32)) &&
                  width(regular) ==
                      width(fonts->GetFontFaceHandle("menulatin", Rml::Style::FontStyle::Normal,
                                                     Rml::Style::FontWeight::Normal, 32)),
              "Authored font style/weight did not select the expected saved/exported faces");
        const Rml::String arabic_language = "ar";
        Rml::TextShapingContext rtl{arabic_language};
        rtl.text_direction = Rml::Style::Direction::Rtl;
        const auto arabic = fonts->GetFontFaceHandle("menuarabic", Rml::Style::FontStyle::Normal,
                                                     Rml::Style::FontWeight::Normal, 32);
        check((fonts->GetStringWidth(regular, "سلام", rtl) ==
               fonts->GetStringWidth(arabic, "سلام", rtl)) == fallback,
              "Authored fallback flag did not control actual missing-glyph native metrics");
        const auto color = input->GetProperty<Rml::Colourb>("color");
        const auto expected = overlay_last ? Rml::Colourb{0x33, 0x55, 0xbb, 255}
                                           : Rml::Colourb{0x11, 0x77, 0x33, 255};
        check(color == expected,
              "Authored stylesheet link order did not control saved/exported native cascade");
        for (const auto size : {Rml::Vector2i{1000, 720}, Rml::Vector2i{600, 400}}) {
            presenter.update(0, size.x, size.y);
            auto* document = Rml::GetContext(0)->GetDocument(0);
            const auto containing = document->GetElementById("preset-center")->GetContainingBlock();
            auto* centered = document->GetElementById("preset-center");
            auto* stretched = document->GetElementById("preset-stretch");
            check(centered->GetProperty<Rml::Colourb>("background-color") ==
                      (size.x >= 800 && size.x <= 1100 ? Rml::Colourb{0x33, 0x55, 0xbb, 255}
                                                       : Rml::Colourb{0x11, 0x77, 0x33, 255}),
                  "Saved/exported native responsive stylesheet lost its active/inactive condition");
            const auto center_offset = centered->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto stretch_offset = stretched->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto center_size = centered->GetBox().GetSize(Rml::BoxArea::Border);
            const auto stretch_size = stretched->GetBox().GetSize(Rml::BoxArea::Border);
            check(containing == Rml::Vector2f{float(size.x), float(size.y)} &&
                      center_size == Rml::Vector2f{120, 40} &&
                      center_offset ==
                          Rml::Vector2f{containing.x / 2 - 60 + 8, containing.y / 2 - 20 + 6} &&
                      stretch_size == Rml::Vector2f{containing.x - 16, containing.y - 12} &&
                      stretch_offset == Rml::Vector2f{8, 6},
                  "Saved/exported native responsive layouts lost viewport behavior");
        }
        check(scene.snapshot() == saved, "Native collection rendering mutated the saved Scene");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(), "Example resources leaked");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need project, scratch and fonts; or --author new-project fonts");
        if (std::string_view(argv[1]) == "--author") {
            author(argv[2], argv[3]);
            return 0;
        }
        if (std::string_view(argv[1]) == "--refresh") {
            const auto root = std::filesystem::absolute(argv[2]);
            const auto font = asset_detail::read_bytes(
                std::filesystem::path(argv[3]) / "LatoLatin-Regular.ttf", 4 * 1024 * 1024);
            const auto settings = ProjectSettings(root).document();
            const std::array roots{settings.at("startup_scene").at("asset").get<AssetId>()};
            ProjectLease lease(root);
            EngineContext engine;
            Scene scene(engine.world());
            prepare_runtime_content_catalog(lease, roots, inspect(root, font), scene.schema());
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{scratch};
        const auto source = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto font = asset_detail::read_bytes(
            std::filesystem::path(argv[3]) / "LatoLatin-Regular.ttf", 4 * 1024 * 1024);
        const auto hud = author_font_selection(source, font);
        for (bool shaped : {false, true})
            present(source, font, scene_path, shaped);
        {
            ProjectLease lease(source);
            const auto css = source / "Assets/UI/hud.rcss";
            const auto text = *asset_storage::read(css);
            asset_storage::replace(
                css, set_ui_stylesheet_font_face_style(text, inspect_ui_stylesheet(text).size() - 1,
                                                       {{"-rmlui-fallback-face", "true"}}));
            const auto document_file = source / hud_path;
            const auto markup = *asset_storage::read(document_file);
            const auto links = read_ui_markup_stylesheet_links(markup);
            asset_storage::replace(
                document_file, move_ui_markup_stylesheet_link(markup, links.back().element, -1));
            refresh_ui_asset_catalog(lease, inspect(source, font)(hud, {}));
        }
        for (bool shaped : {false, true})
            present(source, font, scene_path, shaped, true, false);
        const auto catalog = AssetCatalog::open_project(source);
        ProjectSettings settings(source);
        const std::array roots{settings.document().at("startup_scene").at("asset").get<AssetId>()};
        package_runtime_content(source, scratch / "export", roots, {"windows", "d3d12"}, {}, {}, {},
                                inspect(source, font));
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(scratch / "export", relocated);
        const auto runtime_catalog = open_runtime_content(relocated, {"windows", "d3d12"});
        check(runtime_catalog.records().size() == catalog.records().size(),
              "Collection runtime closure changed identities");
        for (const auto& [id, record] : catalog.records())
            check(runtime_catalog.records().contains(id),
                  "Collection asset ID changed during export");
        std::filesystem::rename(source, scratch / "source-offline");
        for (bool shaped : {false, true})
            present(relocated, font, runtime_catalog.records().at(roots[0]).source, shaped, true,
                    false);
        std::cout << "Font, responsive and ordered stylesheet source-hidden relocated native "
                     "content passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
