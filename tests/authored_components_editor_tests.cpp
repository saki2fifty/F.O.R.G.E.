#define FORGE_UI_FIXTURE 1
#include "../src/authored_schema.hpp"
#include "authored_capture_fixture.hpp"
#include "authored_components.hpp"
#include <iostream>
#include <thread>
using namespace forge;
static void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Health {
    double health;
};
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const auto root =
        std::filesystem::current_path() / ("component-ui-" + AssetId::generate().str());
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    } cleanup{root};
    try {
        const auto capture = test::authored_capture_manifest(1);
        {
            EngineContext validation(WorldRole::Validation);
            detail::validate_authored_types(validation.world().world(), capture.at("components"));
        }
        SceneDocument::create_project(root, "Component authoring UI");
        Json declarations;
        {
            EngineContext source(WorldRole::Validation);
            auto& world = source.world().world();
            const auto type = world.component<Health>().member<double>("health");
            detail::opt_in_authoring(world, type, "project.health", "project.game", 1,
                                     {{"health", 100.0}}, "Gameplay");
            declarations = detail::export_authored_types(world);
        }
        auto settings = ProjectSettings::defaults("Component authoring UI");
        settings["modules"] = Json::array({{{"id", "project.game"},
                                            {"implementation", "1"},
                                            {"sdk", "experimental-1"},
                                            {"fingerprint", std::string(64, 'a')},
                                            {"library", "gameplay.dll"},
                                            {"dependencies", {"forge.transforms"}}}});
        atomic_write(root / "forge.project.json", settings.dump());
        Json manifest{{"format", "forge.authored-types"},
                      {"version", 1},
                      {"profile", "shared-native-sdk"},
                      {"fingerprint", std::string(64, 'a')},
                      {"components", declarations}};
        atomic_write(root / "fixture-manifest.json", manifest.dump());
        atomic_write(root / "fixture.mode", "manifest");
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        AuthoredComponents components;
        PrefabEditor prefab;
        components.project_changed(scene, project);
        const auto runtime = std::filesystem::absolute(argv[1]);
        auto inspect = [&] {
            components.inspect(project, runtime);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (components.busy() && std::chrono::steady_clock::now() < deadline) {
                components.poll(scene, project);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(!components.busy(), "Editor metadata inspection timed out");
        };
        inspect();
        const auto id = authoring_command(scene, "entity.create").at("selected");
        authoring_command(scene, "component.add",
                          {{"entity", id}, {"component", "project.health"}});
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "project.health"},
                           {"field", "health"},
                           {"value", 73.0}});
        const auto before = scene.snapshot();
        const auto schema = scene.schema();
        check(std::filesystem::is_regular_file(root / "forge.components.json"),
              "Editor activation did not preserve migration history");
        atomic_write(root / "fixture.mode", "profile");
        inspect();
        check(scene.snapshot() == before && scene.schema() == schema,
              "Incompatible inspection changed the last admitted editor schema/values");
        atomic_write(root / "fixture.mode", "manifest");
        manifest["components"][0]["schema_version"] = 2;
        atomic_write(root / "fixture-manifest.json", manifest.dump());
        inspect();
        check(scene.snapshot() == before &&
                  detail::AuthoredHistory(root).declarations().size() == 2,
              "New schema activation migrated values or discarded the prior schema");
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(ImTextureID(1));
        for (float scale : {1.f, 1.5f, 2.f}) {
            ui::style(scale);
            // Popup placement measures its first frame before it becomes
            // visible. Inspect settled frames, as the native capture does.
            for (unsigned frame = 0; frame < 3; ++frame) {
                ImGui::NewFrame();
                if (!frame) {
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    components.fixture_open_migration = true;
                }
                ImGui::SetNextWindowSize({950, 850});
                ImGui::Begin("Gameplay Code");
                components.draw(scene, project, prefab, runtime, false);
                ImGui::End();
                ImGui::Render();
            }
            check(ImGui::GetDrawData()->TotalVtxCount > 0, "Component workflow did not render");
            const auto* migration = ImGui::FindWindowByName("Migrate component values");
            check(migration && migration->Active && !migration->Hidden &&
                      migration->Size.x <= io.DisplaySize.x - 30 &&
                      migration->Size.y <= io.DisplaySize.y - 30,
                  "Migration review exceeded the available viewport at this interface scale");
            check(migration->ContentSize.x <= migration->InnerRect.GetWidth(),
                  "Migration rules label extends beyond the available content width");
            check(scene.snapshot() == before,
                  "Opening migration review changed the authored document");
        }
        ImGui::DestroyContext();
        std::cout << "Editor schema worker admission, history, failure retention and scaled "
                     "controls passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
