#include "dependency_automation.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
Json invoke(const std::vector<LiveAuthoringMethod>& methods, const char* name,
            const Json& request = Json::object()) {
    const auto method = std::find_if(methods.begin(), methods.end(),
                                     [&](const auto& value) { return value.name == name; });
    check(method != methods.end(), "Dependency method missing");
    return method->invoke(request);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need dependency lifecycle scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        const auto first = root / "First", second = root / "Second";
        SceneDocument::create_project(first, "First");
        EngineContext engine;
        Scene extra(engine.world());
        extra.save(first / "Scenes/extra.scene.json");
        AssetCatalog initial(first);
        const auto asset = initial.add_scene("Scenes/main.scene.json").id;
        const auto dependency = initial.add_scene("Scenes/extra.scene.json").id;
        initial.set_dependencies(asset, {{dependency,
                                          SceneAsset::type,
                                          AssetDependencyKind::Runtime,
                                          "declared:First project",
                                          {}}});
        initial.save(AssetCatalog::project_index(first));
        std::filesystem::copy(first, second, std::filesystem::copy_options::recursive);
        auto second_catalog = AssetCatalog::open_project(second);
        second_catalog.set_dependencies(asset, {});
        second_catalog.save(AssetCatalog::project_index(second));
        const auto before_first = initial.document(), before_second = second_catalog.document();

        Scene scene(engine.world());
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent);
        files.start(first);
        ui::RuntimeDependenciesEditor owner;
        files.external_busy = [&] { return owner.busy(); };
        files.automation_unavailable = [&](bool switching) -> std::string {
            return switching && owner.dirty() ? "Save or discard dependency draft first" : "";
        };
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(first));
        ui::AutomationPermissions permissions;
        permissions.assets = true;
        const auto methods = DependencyAutomation::methods(
            owner, files, permissions, [&] { return catalog; }, [](AssetId) {}, [] { return ""; });
        const auto old = invoke(methods, "dependencies.open", {{"asset", asset}});
        check(old.at("edges").size() == 1, "Initial dependency review missing");
        if (argc == 3 && std::string_view(argv[2]) != "ui") {
            const auto mode = std::string_view(argv[2]);
            check(mode == "reload" || mode == "retry", "Unknown review recovery case");
            if (mode == "reload") {
                atomic_write(AssetCatalog::project_index(first), "{invalid catalog");
                bool rejected = false;
                try {
                    invoke(methods, "dependencies.open", {{"asset", dependency}});
                } catch (const std::exception&) {
                    rejected = true;
                }
                const auto retained = invoke(methods, "dependencies.read");
                atomic_write(AssetCatalog::project_index(first), initial.document().dump(2));
                check(rejected && retained == old,
                      "Failed dependency reload partially replaced its active review");
                const auto opened = invoke(methods, "dependencies.open", {{"asset", dependency}});
                check(opened.at("asset").get<AssetId>() == dependency &&
                          opened.at("edges").empty() && opened.at("session") != old.at("session"),
                      "Dependency open did not recover after repairing the catalog");
            } else {
                const auto edited =
                    invoke(methods, "dependencies.edit",
                           {{"state", old.at("state")}, {"dependencies", Json::array()}});
                auto external = initial;
                auto changed = external.records().at(dependency);
                changed.metadata["fixture"] = "external catalog change";
                external.replace(std::move(changed));
                external.save(AssetCatalog::project_index(first));
                catalog = std::make_shared<const AssetCatalog>(external);
                auto settle = [&] {
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (owner.busy()) {
                        owner.poll(files.document, [&] {
                            catalog = std::make_shared<const AssetCatalog>(
                                AssetCatalog::open_project(first));
                        });
                        check(std::chrono::steady_clock::now() < deadline,
                              "Review worker did not settle");
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                };
                invoke(methods, "dependencies.save", {{"state", edited.at("state")}});
                settle();
                const auto failed = invoke(methods, "dependencies.read");
                check(owner.dirty() && failed.at("error") != "" && failed.at("operation") != "" &&
                          failed.at("edges").empty() &&
                          AssetCatalog::open_project(first).document() == external.document(),
                      "Conflicting save did not retain the draft and persisted catalog");
                atomic_write(AssetCatalog::project_index(first), "{invalid catalog");
                bool rejected = false;
                try {
                    invoke(methods, "dependencies.discard", {{"state", failed.at("state")}});
                } catch (const std::exception&) {
                    rejected = true;
                }
                const auto retained = invoke(methods, "dependencies.read");
                atomic_write(AssetCatalog::project_index(first), external.document().dump(2));
                check(rejected && retained == failed,
                      "Failed refresh discarded the failed-save draft/error");
                const auto refreshed =
                    invoke(methods, "dependencies.discard", {{"state", failed.at("state")}});
                check(!owner.dirty() && refreshed.at("error") == "" &&
                          refreshed.at("operation") == "" && refreshed.at("edges").size() == 1 &&
                          refreshed.at("session") != failed.at("session"),
                      "Fresh dependency review retained the obsolete failure/operation");
                const auto started =
                    invoke(methods, "dependencies.save", {{"state", refreshed.at("state")}});
                settle();
                const auto saved = invoke(methods, "dependencies.read");
                check(!owner.dirty() && saved.at("error") == "" &&
                          saved.at("operation") == started.at("operation") &&
                          saved.at("operation") != failed.at("operation"),
                      "Successful retry lost its own operation attribution");
                const auto another = invoke(methods, "dependencies.open", {{"asset", dependency}});
                check(another.at("operation") == "" && another.at("error") == "",
                      "Opening another asset inherited the previous save result");
            }
            check(AssetCatalog::open_project(second).document() == before_second &&
                      !scene.can_undo() && !files.document.dirty(),
                  "Review recovery changed scene history or another project");
            std::cout << "Dependency " << mode << " recovery passed\n";
            return 0;
        }

        ImGui::CreateContext();
        struct UiCleanup {
            ~UiCleanup() { ImGui::DestroyContext(); }
        } ui_cleanup;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1100, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::style(1);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        AssetId inspected = asset;
        auto frame = [&](bool expanded) {
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({600, 800});
            ImGui::Begin("Dependency fixture");
            ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);
            owner.draw(files.document, *catalog, catalog->records().at(inspected), false,
                       [](const AssetRecord&) {});
            ImGui::End();
            ImGui::Render();
        };
        if (argc == 3) {
            inspected = dependency;
            atomic_write(AssetCatalog::project_index(first), "{invalid catalog");
            bool escaped = false;
            try {
                frame(true);
            } catch (const std::exception&) {
                escaped = true;
            }
            atomic_write(AssetCatalog::project_index(first), initial.document().dump(2));
            check(!escaped, "Dependency reload error escaped the native Inspector");
            const auto retained = invoke(methods, "dependencies.read");
            check(retained.at("asset") == old.at("asset") &&
                      retained.at("edges") == old.at("edges") &&
                      retained.at("session") == old.at("session"),
                  "Inspector reload failure lost the previous review");
            frame(true);
            const auto loaded = invoke(methods, "dependencies.read");
            check(loaded.at("asset").get<AssetId>() == dependency && loaded.at("edges").empty(),
                  "Inspector did not recover after the catalog was repaired");
            check(!scene.can_undo() && !files.document.dirty(),
                  "Inspector reload changed scene history");
            std::cout << "Inspector dependency reload error/recovery passed\n";
            return 0;
        }
        frame(false);
        check(invoke(methods, "dependencies.read").at("state") == old.at("state"),
              "Same-project collapsed draw replaced its review");
        files.changed = false;
        files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(second));
        check(!invoke(methods, "dependencies.read").at("open").get<bool>(),
              "Hidden dependency review crossed projects");
        frame(false);
        check(!invoke(methods, "dependencies.read").at("open").get<bool>(),
              "Collapsed dependency UI revived a previous-project review");
        frame(true);
        const auto current = invoke(methods, "dependencies.read");
        check(current.at("open").get<bool>() && current.at("edges").empty() &&
                  current.at("session") != old.at("session"),
              "Copied asset ID retained previous-project edges/session");
        bool stale = false;
        try {
            invoke(methods, "dependencies.edit",
                   {{"state", old.at("state")}, {"dependencies", Json::array()}});
        } catch (const AuthoringError& error) {
            stale = error.code == "stale_dependencies";
        }
        check(stale && !owner.dirty(), "Old review digest edited the new project");
        check(AssetCatalog::open_project(first).document() == before_first &&
                  AssetCatalog::open_project(second).document() == before_second &&
                  !scene.can_undo() && !files.document.dirty(),
              "Reviewing dependencies changed authored content/history");
        const auto rows = Json::array(
            {{{"asset", dependency}, {"type", SceneAsset::type}, {"reason", "Second project"}}});
        auto edited = invoke(methods, "dependencies.edit",
                             {{"state", current.at("state")}, {"dependencies", rows}});
        auto blocked_switch = [&] {
            files.changed = false;
            bool blocked = false;
            try {
                files.automation_project({EditorFiles::Command::OpenProject, first, {}}, false);
            } catch (const std::exception&) {
                blocked = true;
            }
            check(blocked && files.document.project() == second,
                  "Dependency draft/job failed to protect the current project");
        };
        blocked_switch();
        owner.poll(files.document, [] {});
        frame(false);
        check(owner.dirty() &&
                  invoke(methods, "dependencies.read").at("state") == edited.at("state"),
              "Rejected switch or collapsed panel discarded the draft");
        const auto discarded =
            invoke(methods, "dependencies.discard", {{"state", edited.at("state")}});
        check(!owner.dirty() && discarded.at("edges").empty(),
              "Discard did not reload this project");
        edited = invoke(methods, "dependencies.edit",
                        {{"state", discarded.at("state")}, {"dependencies", rows}});
        invoke(methods, "dependencies.save", {{"state", edited.at("state")}});
        check(owner.busy(), "Native dependency save did not start");
        blocked_switch();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            owner.poll(files.document, [&] {
                catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(second));
            });
            check(std::chrono::steady_clock::now() < deadline, "Dependency save did not settle");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (owner.busy());
        const auto saved = invoke(methods, "dependencies.read");
        check(!owner.dirty() && saved.at("error") == "" && saved.at("operation") != "" &&
                  saved.at("edges").at(0).at("role") == "declared:Second project",
              "Dependency save did not publish the new project's review");
        check(AssetCatalog::open_project(first).document() == before_first &&
                  AssetCatalog::open_project(second).document() == catalog->document() &&
                  catalog->document() != before_second && !scene.can_undo() &&
                  !files.document.dirty(),
              "Dependency publication changed the wrong project or scene history");
        frame(false);
        check(invoke(methods, "dependencies.read").at("operation") == saved.at("operation"),
              "Same-project drawing erased the completed operation");
        // Draw must invalidate independently, without an earlier automation read/poll.
        files.changed = false;
        files.automation_project({EditorFiles::Command::OpenProject, first, {}}, false);
        catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(first));
        frame(false);
        check(!invoke(methods, "dependencies.read").at("open").get<bool>(),
              "UI-only switch retained the old review");
        frame(true);
        check(invoke(methods, "dependencies.read").at("edges").at(0).at("role") ==
                  "declared:First project",
              "Returning to the first project did not reload its declarations");
        // The read route also observes changes before a panel or main-loop poll.
        files.changed = false;
        files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(second));
        check(!invoke(methods, "dependencies.read").at("open").get<bool>(),
              "Read before UI/poll exposed the prior project");
        frame(true);
        // A closed Inspector can visit another project and return without drawing
        // this panel or making an automation read. The main-loop poll must observe it.
        files.changed = false;
        files.automation_project({EditorFiles::Command::OpenProject, first, {}}, false);
        owner.poll(files.document, [] {});
        files.changed = false;
        files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        owner.poll(files.document, [] {});
        check(!invoke(methods, "dependencies.read").at("open").get<bool>(),
              "Closed-panel round trip revived a completed operation");
        frame(true);
        const auto reopened = invoke(methods, "dependencies.read");
        check(reopened.at("session") != saved.at("session") && reopened.at("operation") == "" &&
                  reopened.at("edges") == saved.at("edges"),
              "Reopening lost saved declarations or retained the old operation");
        std::cout << "Runtime dependency review remains scoped to its project\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
