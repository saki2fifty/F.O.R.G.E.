#include "asset_bytes.hpp"
#include "project_text_source.hpp"
#include "runtime_package.hpp"
#include "ui_markup_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <forge/authoring.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/210-ui-source-authoring.scene.json";
constexpr auto document_path = "Assets/UI/SourceAuthoring/hud.rml";
constexpr auto stylesheet_path = "Assets/UI/SourceAuthoring/hud.rcss";
constexpr auto layout_scene = "Scenes/211-ui-layout-preview.scene.json";
constexpr auto layout_document = "Assets/UI/Layout/layout.rml";
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read_json(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
std::size_t by_id(const std::string& source, std::string_view id) {
    const auto nodes = inspect_ui_markup(source);
    for (std::size_t i = 0; i < nodes.size(); ++i)
        for (const auto& attribute : nodes[i].attributes)
            if (attribute.name == "id" &&
                std::string_view(source).substr(attribute.value.begin,
                                                attribute.value.end - attribute.value.begin) == id)
                return i;
    throw std::runtime_error("UI source example element missing");
}
RuntimeUiInspector inspector(const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [=](AssetId id, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, root, font);
        return presenter.inspect_static({id});
    };
}
void author(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    check(!std::filesystem::exists(root / scene_path) &&
              !std::filesystem::exists(root / document_path),
          "UI source example already exists");
    std::filesystem::create_directories((root / document_path).parent_path());
    std::string markup =
        "<!-- FORGE source authoring example -->\n<rml><head><link type=\"text/rcss\" "
        "href=\"hud.rcss\" /></head><body id=\"canvas\"><div id=\"panel\"></div></body></rml>\n";
    const auto append = [&](std::string_view tag, std::string id, std::string_view text) {
        markup = insert_ui_markup_child(markup, by_id(markup, "panel"), tag, text);
        const auto nodes = inspect_ui_markup(markup);
        // The appended direct child is last in source order; this example adds leaves only.
        markup = set_ui_markup_attribute(markup, nodes.size() - 1, "id", std::move(id));
    };
    append("p", "eyebrow", "FORGE / UI SOURCE AUTHORING");
    append("p", "heading", "Edit the source. Run the result.");
    append("p", "message", "Text & markup: <your own HUD>");
    append("p", "state", "Tick {{tick}} / paused {{paused}}");
    for (const auto& name : {"Pause", "Resume", "Step"}) {
        append("button", name, name);
        markup = set_ui_markup_attribute(markup, by_id(markup, name), "data-event-click",
                                         "command('" + std::string(name) + "')");
    }
    append("input", "name", {});
    markup = set_ui_markup_attribute(markup, by_id(markup, "name"), "type", "text");
    markup = set_ui_markup_attribute(markup, by_id(markup, "name"), "value", "Your name");
    append("p", "hint",
           "Stop Play. In Content, edit hud.rml with Source or Elements. Save, "
           "then start Play again. Edit hud.rcss to change layout and colors.");
    atomic_write(root / document_path, markup);
    atomic_write(
        root / stylesheet_path,
        R"rcss(body {width:100%;height:100%;font-family:Lato;font-size:18px;color:#edf3fa;pointer-events:none;}
body,div,p {display:block;} p {margin:10px 0;}
#panel {position:absolute;left:5%;top:5%;width:90%;max-width:660px;max-height:90%;box-sizing:border-box;padding:22px;overflow:auto;background-color:#162b3bf2;pointer-events:auto;}
#eyebrow {font-size:14px;color:#73d7bd;} #heading {font-size:28px;color:#ffffff;}
#state {padding:10px;background-color:#24485f;} #hint {font-size:15px;color:#bbcad6;}
button {display:inline-block;padding:10px 14px;margin:10px 7px 14px 0;background-color:#345a77;}
button:hover,button:focus {background-color:#487f9e;}
input {display:block;width:240px;height:32px;background-color:#0f202f;color:#ffffff;}
scrollbarvertical {width:12px;background-color:#101d29;} scrollbarvertical sliderbar {background-color:#487f9e;}
)rcss");
    const auto ui = register_ui_document(root, document_path);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read_json(root / "Scenes/100-ui-animation.scene.json"),
                                        AssetId::generate(), scene.schema());
    for (auto& row : source["entities"])
        if (row["components"].contains("forge.ui_document")) {
            row["name"] = "HUD - edit Source or Elements";
            row["components"]["forge.ui_document"]["document"] = ui.id;
        }
    scene.reset(source);
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    ProjectLease lease(root);
    const std::array roots{record.id};
    prepare_runtime_content_catalog(lease, roots, inspector(root, font), scene.schema());
}
void exercise(const std::filesystem::path& root, const Json& source,
              const std::vector<std::byte>& font, bool external_conflict) {
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        presenter.reset("ui-source", 1);
        auto snapshot = ui->snapshot(scene, "ui-source", 1, 41, true);
        check(presenter.accept(snapshot), presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        presenter.render();
        auto* document = Rml::GetContext(0)->GetDocument(0);
        check(document->GetElementById("message")->GetInnerRML().find("&lt;your own HUD&gt;") !=
                  std::string::npos,
              "Escaped source text was parsed as child markup");
        const auto initial_state = document->GetElementById("state")->GetInnerRML();
        if (initial_state != "Tick 41 / paused 1")
            throw std::runtime_error("Structured text binding result: " + initial_state);
        check(renderer.draws && !renderer.textures.empty() && presenter.asset_snapshot() &&
                  presenter.asset_snapshot()->sources.size() == 2,
              "UI source example lost text draw resources or linked stylesheet");
        check(document->GetElementById("heading")->GetProperty<float>("font-size") == 28,
              "Linked stylesheet layout was not applied");
        const auto position = document->GetElementById("Resume")->GetAbsoluteOffset();
        presenter.mouse_move(int(position.x + 5), int(position.y + 5), 0);
        presenter.mouse_button(0, true, 0);
        presenter.mouse_button(0, false, 0);
        const auto command = presenter.pending_command();
        check(command.has_value(), "Native example button did not enqueue a command");
        std::string operation;
        ui->command(scene, *command, [&](const std::string& name) { operation = name; });
        check(operation == "Resume", "Native command did not reach the current UiRuntime");
        auto ack = *command;
        ack["ok"] = true;
        presenter.acknowledge(ack);
        snapshot = ui->snapshot(scene, "ui-source", 1, 42, false);
        check(presenter.accept(snapshot), presenter.diagnostic().c_str());
        presenter.update(.02, 800, 600);
        check(document->GetElementById("state")->GetInnerRML() == "Tick 42 / paused 0",
              "Play model update did not reach the authored source");
        if (external_conflict) {
            const auto path = root / document_path;
            const auto saved = ProjectTextSource::read(path);
            check(saved.has_value(), "Saved source example disappeared");
            atomic_write(path, "<rml><body><script>forbidden</script></body></rml>");
            check(!presenter.reload() && presenter.document_count() == 1,
                  "Invalid external source discarded last-good UI");
            atomic_write(path, *saved);
            check(presenter.reload(), presenter.diagnostic().c_str());
            presenter.update(.04, 800, 600);
            presenter.render();
        }
        presenter.reset("ui-source-retired", 2);
        check(Rml::GetNumContexts() == 0, "Retired source UI retained a native context");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Source UI native resources survived presenter retirement");
    check(scene.snapshot() == source, "UI interaction changed authored Scene state");
}
void author_layout(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    check(!std::filesystem::exists(root / layout_scene), "UI layout example already exists");
    const auto ui = register_ui_document(root, layout_document);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read_json(root / "Scenes/100-ui-animation.scene.json"),
                                        AssetId::generate(), scene.schema());
    for (auto& row : source["entities"])
        if (row["components"].contains("forge.ui_document")) {
            row["name"] = "UI layout - open Preview";
            row["components"]["forge.ui_document"]["document"] = ui.id;
        }
    scene.reset(source);
    scene.save(root / layout_scene);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(layout_scene);
    catalog.save(AssetCatalog::project_index(root));
    ProjectLease lease(root);
    const std::array roots{record.id};
    prepare_runtime_content_catalog(lease, roots, inspector(root, font), scene.schema());
}
void exercise_layout(const std::filesystem::path& root, const Json& source,
                     const std::vector<std::byte>& font, bool edit_source) {
    AssetId asset;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.ui_document"))
            asset = row.at("components").at("forge.ui_document").at("document").get<AssetId>();
    check(bool(asset), "UI layout Scene has no assigned document");
    const auto file = ProjectTextSource::path(root, layout_document);
    auto text = ProjectTextSource::read(file);
    check(text.has_value(), "UI layout example source missing");
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        if (edit_source) {
            const auto candidate =
                set_ui_markup_layout(*text, by_id(*text, "panel"), {150, 180, 250, 190});
            check(presenter.preview_document({asset}, layout_document, candidate, 800, 600),
                  "Gallery draft preview did not load");
            const auto view = presenter.preview_layout();
            check(view.at("elements").at(0).at("editable").get<bool>() &&
                      view.at("elements").at(0).at("bounds") == Json::array({150, 180, 250, 190}),
                  "Gallery draft preview lost pixel layout eligibility");
            check(ProjectTextSource::read(file) == text && !presenter.asset_snapshot(),
                  "Gallery preview published source or catalog observations");
            ProjectLease lease(root);
            ProjectTextSource::validate(file, candidate);
            ProjectTextSource::write_validated(file, text, candidate);
            text = candidate;
        }
        presenter.reset("layout-runtime", 1);
        check(presenter.accept(ui->snapshot(scene, "layout-runtime", 1, 0, false)),
              "Saved layout example did not load in runtime UI");
        presenter.update(0, 800, 600);
        presenter.render();
        auto* panel = Rml::GetContext(0)->GetDocument(0)->GetElementById("panel");
        const auto position = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
        check(position.x == 150 && position.y == 180 && size.x == 250 && size.y == 190 &&
                  renderer.draws,
              "Saved/exported UI did not retain the previewed layout");
    }
    check(scene.snapshot() == source && renderer.geometry.empty() && renderer.textures.empty(),
          "Layout workflow changed Scene data or leaked native resources");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || (argc == 5 && std::string_view(argv[4]) == "--layout-only"),
              "Need Gallery, scratch root and font; or --author/--author-layout Gallery font");
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        if (std::string_view(argv[1]) == "--author") {
            author(argv[2], font);
            return 0;
        }
        if (std::string_view(argv[1]) == "--author-layout") {
            author_layout(argv[2], font);
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(scratch);
        const auto root = scratch / "source";
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const bool layout_only = argc == 5;
        const auto source = read_json(root / (layout_only ? layout_scene : scene_path));
        if (layout_only)
            exercise_layout(root, source, font, true);
        else
            exercise(root, source, font, true);
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
        check(catalog.records().size() == 3, "UI source package lost/duplicated its closure");
        std::filesystem::rename(root, scratch / "source-offline");
        const auto exported = read_json(moved / catalog.records().at(roots[0]).source);
        if (layout_only)
            exercise_layout(moved, exported, font, false);
        else
            exercise(moved, exported, font, false);
        std::cout << "UI source structured authoring, native bindings/input/last-good, linked "
                     "resources and source-hidden relocated content passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
