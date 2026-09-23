#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_package.hpp"
#include "ui_asset_catalog.hpp"
#include "ui_inspection.hpp"
#include "ui_test_renderer.hpp"
#include <forge/scene.hpp>
#include <forge/ui_presenter.hpp>
#include <iostream>
#include <source_location>
using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F&& f, std::string_view code,
             std::source_location loc = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception& e) {
        if (std::string_view(e.what()).find(code) != std::string_view::npos)
            return;
        throw std::runtime_error("Unexpected rejection at " + std::to_string(loc.line()) + ": " +
                                 e.what());
    }
    throw std::runtime_error("Missing rejection at " + std::to_string(loc.line()));
}
Json state(AssetId id, bool visible = true) {
    const auto e = EntityId::generate();
    return {{"version", 1},
            {"session", "package"},
            {"generation", 1u},
            {"revision", 1u},
            {"documents", Json::array({{{"entity", e},
                                        {"asset", id},
                                        {"instance", e.str()},
                                        {"visible", visible},
                                        {"layer", 0u},
                                        {"model", {{"tick", 0u}, {"paused", false}}},
                                        {"commands", {"Pause", "Resume", "Step"}}}})}};
}
void trigger(UiPresenter& p, std::string_view pseudo) {
    auto* document = Rml::GetContext(0)->GetDocument(0);
    auto* button = document->GetElementById("button");
    check(button != nullptr, "Native button missing");
    const auto pos = button->GetAbsoluteOffset();
    p.mouse_move(int(pos.x + 8), int(pos.y + 8), 0);
    if (pseudo != "hover")
        p.mouse_button(0, true, 0);
    if (pseudo == "focus")
        p.mouse_button(0, false, 0);
    p.update(1, 800, 600);
    p.render();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need scratch, font and native inspector");
        const auto scratch = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto project = scratch / "authoring";
        std::filesystem::create_directories(project);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{scratch};
        const auto font = asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        const auto worker = std::filesystem::absolute(argv[3]);
        const auto font_path = std::filesystem::absolute(argv[2]);
        std::string tga(21, '\0');
        tga[2] = 2;
        tga[12] = tga[14] = 1;
        tga[16] = 24;
        tga[17] = 32;
        tga[20] = char(255);
        asset_storage::replace(project / "later.tga", tga);
        asset_storage::replace(project / "literal.tga", tga);
        asset_storage::replace(project / "hidden.tga", tga);
        asset_storage::replace(project / "never.tga", tga);
        asset_storage::replace(
            project / "hud.rcss",
            "body {font-family:Lato;} button {width:180px;height:40px;} #hidden {display:none;}");
        const auto markup = [](std::string_view pseudo) {
            if (pseudo == "media")
                return std::string(
                    "<rml><head><style>body{font-family:Lato;} button{width:180px;height:40px;} "
                    "@media (min-width:1600px) "
                    "{button{decorator:image(later.tga);}}</style></head><body><button "
                    "id=\"button\">Hello {{runtime_value}}</button></body></rml>");
            return "<rml><head><link type=\"text/rcss\" href=\"hud.rcss\"/><style>button:" +
                   std::string(pseudo) +
                   " {decorator:image(later.tga);}</style></head><body><button id=\"button\">Hello "
                   "{{runtime_value}}</button>"
                   "<img src=\"literal.tga\"/><div id=\"hidden\"><img "
                   "src=\"hidden.tga\"/></div></body></rml>";
        };
        asset_storage::replace(project / "hud.rml", markup("hover"));
        AssetId document, later, never;
        {
            ProjectLease writer(project);
            document = register_ui_source(writer, "hud.rml").id;
            later = register_ui_source(writer, "later.tga").id;
            never = register_ui_source(writer, "never.tga").id;
        }
        RuntimeUiInspector inspect = [&](AssetId id, std::stop_token stop) {
            return inspect_ui_dependencies(worker, font_path, project, id, stop);
        };
        // Automatically discovered logical assets must get durable catalog IDs
        // before low-level immutable packaging, not random per-export identities.
        asset_storage::replace(project / "fresh.rcss", "body{font-family:Lato;color:blue;}");
        asset_storage::replace(project / "fresh.rml",
                               "<rml><head><link type=\"text/rcss\" "
                               "href=\"fresh.rcss\"/></head><body>Fresh</body></rml>");
        {
            ProjectLease writer(project);
            const auto fresh = register_ui_source(writer, "fresh.rml");
            const std::array fresh_roots{fresh.id};
            rejects(
                [&] {
                    package_runtime_content(project, scratch / "unregistered", fresh_roots,
                                            {"linux", "none"}, {}, {}, {}, inspect);
                },
                "export.ui.registration.required");
            prepare_runtime_content_catalog(writer, fresh_roots, inspect);
            const auto registered = AssetCatalog::open_project(project).document();
            prepare_runtime_content_catalog(writer, fresh_roots, inspect);
            check(AssetCatalog::open_project(project).document() == registered,
                  "UI identity preparation reminted logical asset identities");
        }
        auto snapshot = inspect(document, {});
        {
            UiTestRenderer renderer;
            UiPresenter p(renderer, project, font);
            check(p.inspect_static({document}).automatic_sources == snapshot.automatic_sources,
                  "Native worker and in-process static admission disagree");
        }
        check(snapshot.automatic_sources.contains("hud.rcss") &&
                  snapshot.automatic_sources.contains("literal.tga") &&
                  snapshot.automatic_sources.contains("hidden.tga") &&
                  !snapshot.automatic_sources.contains("later.tga"),
              "Native automatic subset incorrect");
        // Preview observation is deliberately not a declaration, including old Runtime edges.
        {
            ProjectLease writer(project);
            auto catalog = refresh_ui_asset_catalog(writer, snapshot);
            auto r = catalog.records().at(document);
            r.dependency_edges.push_back(
                {later, "texture", AssetDependencyKind::Runtime, "ui.observed", {}});
            r.dependencies.push_back(later);
            catalog.replace(r);
            catalog.save(project / "forge.assets.json");
        }
        const std::array roots{document};
        const RuntimePackageTarget target{"linux", "none"};
        const auto original_catalog = AssetCatalog::open_project(project).document();
        package_runtime_content(project, scratch / "undeclared", roots, target, {}, {}, {},
                                inspect);
        check(AssetCatalog::open_project(project).document() == original_catalog,
              "Export mutated source catalog");
        check(!open_runtime_content(scratch / "undeclared", target).records().contains(later),
              "Observed image silently promoted into package");
        std::filesystem::rename(project, scratch / "offline");
        {
            UiTestRenderer renderer;
            UiPresenter p(renderer, scratch / "undeclared", font);
            p.reset("package", 1);
            auto initial = state(document);
            initial["documents"][0]["model"]["runtime_value"] = "Ready";
            check(p.accept(initial), p.diagnostic().c_str());
            trigger(p, "hover");
            check(p.diagnostic().find("package.resource.undeclared") != std::string::npos,
                  "Hover must reject undeclared image after initial preview succeeds");
        }
        std::filesystem::rename(scratch / "offline", project);
        for (const std::string pseudo : {"hover", "focus", "active", "media"}) {
            asset_storage::replace(project / "hud.rml", markup(pseudo));
            {
                ProjectLease writer(project);
                auto catalog = refresh_ui_asset_catalog(writer, inspect(document, {}));
                catalog = declare_runtime_dependencies(writer, document,
                                                       {{later,
                                                         "texture",
                                                         AssetDependencyKind::Runtime,
                                                         "declared:UI interaction",
                                                         {}},
                                                        {never,
                                                         "texture",
                                                         AssetDependencyKind::Runtime,
                                                         "declared:theme alternatives",
                                                         {}}},
                                                       catalog.document());
                validate_runtime_declarations(project, catalog.records().at(document));
            }
            const auto package = scratch / pseudo;
            package_runtime_content(project, package, roots, target, {}, {}, {}, inspect);
            const auto catalog = open_runtime_content(package, target);
            check(catalog.records().contains(later) && catalog.records().contains(never),
                  "Declared never-observed finite resource missing");
            std::filesystem::rename(project, scratch / "offline");
            {
                UiTestRenderer renderer;
                UiPresenter p(renderer, package, font);
                p.reset("package", 1);
                auto initial = state(document, false);
                initial["documents"][0]["model"]["runtime_value"] = "Ready";
                check(p.accept(initial), p.diagnostic().c_str());
                initial["revision"] = 2u;
                initial["documents"][0]["visible"] = true;
                check(p.accept(initial), p.diagnostic().c_str());
                if (pseudo == "media") {
                    p.update(2, 1800, 1000);
                    p.render();
                } else
                    trigger(p, pseudo);
                check(p.diagnostic().empty(), p.diagnostic().c_str());
                check(std::any_of(renderer.loaded_sources.begin(), renderer.loaded_sources.end(),
                                  [](const auto& path) { return path.ends_with("later.tga"); }),
                      "Conditional state did not actually load its declared texture");
            }
            std::filesystem::rename(scratch / "offline", project);
        }
        asset_storage::replace(project / "hud.rml", markup("hover"));
        rejects(
            [&] {
                package_runtime_content(project, scratch / "stale", roots, target, {}, {}, {},
                                        inspect);
            },
            "export.declarations.stale");
        {
            ProjectLease writer(project);
            auto catalog = refresh_ui_asset_catalog(writer, inspect(document, {}));
            const auto before = catalog.document();
            rejects(
                [&] {
                    declare_runtime_dependencies(writer, document,
                                                 {{AssetId::generate(),
                                                   "texture",
                                                   AssetDependencyKind::Runtime,
                                                   "declared:missing",
                                                   {}}},
                                                 before);
                },
                "export.declarations.missing");
            rejects(
                [&] {
                    declare_runtime_dependencies(
                        writer, document,
                        {{later, "mesh", AssetDependencyKind::Runtime, "declared:wrong type", {}}},
                        before);
                },
                "export.declarations.missing");
            check(AssetCatalog::open_project(project).document() == before,
                  "Rejected declaration mutated catalog");
        }
        auto corrupt = scratch / "hover" / "never.tga";
        asset_storage::replace(corrupt, "bad");
        rejects([&] { open_runtime_content(scratch / "hover", target); }, "hash");
        // Content can display discovered documents absent from the persisted index.
        // Saving their declarations must register only requested documents in the
        // same candidate, never compare that discovered view as a disk revision.
        {
            WorldContext world;
            Scene scene(world);
            const auto id = scene.snapshot().at("asset_id").get<AssetId>();
            scene.save(project / "discovered.scene.json");
            const auto prefab = AssetId::generate();
            const auto member = PrefabMemberId::generate();
            asset_storage::replace(
                project / "discovered.prefab.json",
                Json{{"format", "forge.prefab"},
                     {"version", 2},
                     {"asset_id", prefab},
                     {"revision", 1u},
                     {"root", member},
                     {"members",
                      Json::array(
                          {{{"id", member}, {"name", "Part"}, {"components", Json::object()}}})}}
                    .dump());
            ProjectLease writer(project);
            const auto expected = AssetCatalog::open_project(project).document();
            const std::array<AssetRecord, 2> discoveries{
                {{id, "scene", "discovered.scene.json", 3},
                 {prefab, "prefab", "discovered.prefab.json", 2}}};
            rejects(
                [&] {
                    declare_runtime_dependencies(writer, id,
                                                 {{AssetId::generate(),
                                                   "texture",
                                                   AssetDependencyKind::Runtime,
                                                   "declared:missing",
                                                   {}}},
                                                 expected, nullptr, discoveries);
                },
                "discovery");
            check(AssetCatalog::open_project(project).document() == expected,
                  "Rejected declaration published document discovery");
            auto wrong = discoveries;
            wrong[0].id = AssetId::generate();
            const std::vector<AssetDependency> edges{
                {prefab, "prefab", AssetDependencyKind::Runtime, "declared:runtime spawn", {}}};
            rejects(
                [&] {
                    declare_runtime_dependencies(writer, wrong[0].id, edges, expected, nullptr,
                                                 wrong);
                },
                "identity changed");
            const auto saved =
                declare_runtime_dependencies(writer, id, edges, expected, nullptr, discoveries);
            check(saved.records().contains(id) && saved.records().contains(prefab) &&
                      saved.records().at(id).dependency_edges == edges,
                  "Discovered owner/target declarations did not publish together");
            rejects(
                [&] {
                    declare_runtime_dependencies(writer, id, {}, expected, nullptr, discoveries);
                },
                "conflict");
            check(AssetCatalog::open_project(project).document() == saved.document(),
                  "Stale declaration overwrote the accepted graph");
        }
        std::cout << "Native static UI inspection, advisory separation, declared finite closure, "
                     "relocated conditional loads, stale/missing/type rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
