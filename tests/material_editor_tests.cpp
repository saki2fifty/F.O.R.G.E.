#include "component_inspector.hpp"
#include "material_editor.hpp"
#include "mesh_material_inspector.hpp"
#include "surface_material_fixture.hpp"
#include <iostream>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need material editor scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root, true);
        auto source =
            MaterialDocument::create(project.writer_guard(), "Assets/source.material.json");
        const auto id = source->source().asset();
        source.reset();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1000, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        MaterialEditor editor;
        unsigned previews = 0, releases = 0;
        std::optional<MaterialResourceData> preview;
        editor.update_preview = [&](auto ref, auto data, auto) {
            require(ref.id == id, "Preview identity changed");
            preview = std::move(data);
            ++previews;
        };
        editor.release_preview = [&] {
            preview.reset();
            ++releases;
        };
        std::string message;
        auto frame = [&] {
            editor.poll(project, message);
            ImGui::NewFrame();
            editor.draw(project, false);
            ImGui::Render();
        };
        auto finish = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            do {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (editor.pending() && std::chrono::steady_clock::now() < deadline);
            require(!editor.pending(), "Material UI publication stalled");
        };
        editor.open(project, "Assets/source.material.json");
        frame();
        require(editor.is_open() && editor.dirty() && previews == 1 && preview,
                "New source did not open an independent draft/preview");
        editor.edit(editor.document()->revision(), "Color", [](auto& j) {
            j["overrides"]["parameters"]["baseColorFactor"] = {
                {"type", unsigned(MaterialParameterType::LinearColor4)}, {"value", {1, 0, 0, 1}}};
        });
        frame();
        require(editor.can_undo() &&
                    preview->values.parameters.at("baseColorFactor").value[0] == 1 &&
                    preview->values.parameters.at("baseColorFactor").value[1] == 0,
                "Source edit did not update detached preview/history");
        editor.undo();
        frame();
        require(editor.can_redo() && !preview->values.parameters.contains("baseColorFactor"),
                "Source Undo did not remove factor override");
        editor.redo();
        frame();
        editor.request_close();
        frame();
        require(ImGui::GetTopMostPopupModal() != nullptr, "Material close omitted draft guard");
        editor.request_save();
        finish();
        frame();
        require(!editor.is_open() && !editor.dirty() && !preview && releases >= 1,
                "Successful material Save did not finish guarded close/release preview");
        require(ImGui::GetTopMostPopupModal() == nullptr, "Material Save left a stale modal");
        auto good = AssetCatalog::open_project(root).document();
        require(AssetCatalog::open_project(root).records().contains(id),
                "Material UI did not publish original UUID");
        editor.open(project, "Assets/source.material.json");
        frame();
        {
            const auto prior_releases = releases;
            MaterialDocument external(project.writer_guard(), "Assets/source.material.json");
            external.edit(external.revision(), "External roughness", [](auto& j) {
                j["overrides"]["parameters"]["roughnessFactor"] = {{"type", 0}, {"value", {.42f}}};
            });
            external.save();
            AssetImportService service(project.writer_guard(), material_import_registry(),
                                       desktop_texture_target());
            service.submit(
                service.prepare("Assets/source.material.json"),
                [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); },
                [](const auto&, const auto&) {});
            require(service.wait_idle(std::chrono::seconds(10)),
                    "External source publication stalled");
            const auto outcome = service.poll();
            require(outcome.size() == 1 && outcome[0].published,
                    "External source publication failed");
            editor.source_published(project, id);
            editor.asset_catalog_changed(
                std::make_shared<const AssetCatalog>(outcome[0].publication->catalog));
            frame();
            require(!editor.dirty() && !editor.can_undo() && releases == prior_releases &&
                        preview->values.parameters.at("roughnessFactor").value[0] == .42f,
                    "Clean material hot reload lost preview ownership or retained stale source");
            good = outcome[0].publication->catalog.document();
        }
        const auto before_invalid = previews;
        editor.edit(editor.document()->revision(), "Invalid roughness", [](auto& j) {
            j["overrides"]["parameters"]["roughnessFactor"] = {{"type", 0}, {"value", {-1}}};
        });
        frame();
        require(previews == before_invalid && preview, "Invalid source replaced last-good preview");
        editor.request_save();
        finish();
        frame();
        require(editor.is_open() && editor.dirty() && !editor.document()->dirty(),
                "Source Save and rejected publication ownership were conflated");
        require(AssetCatalog::open_project(root).document() == good &&
                    !context.problems.items().empty(),
                "Invalid material published or lost diagnostics");
        require(!project.dirty(), "Material editing dirtied authored scene");
        MeshMaterialInspector slots;
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        slots.select(root, catalog, engine_primitive(0));
        const auto slot_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (slots.pending() && std::chrono::steady_clock::now() < slot_deadline) {
            slots.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(!slots.pending() && slots.error().empty() && slots.bindings().size() == 1 &&
                    slots.bindings()[0].key == "surface",
                "Inspector did not resolve logical engine mesh slots asynchronously");
        const std::string entity =
            authoring_command(scene, "entity.create", {{"recipe", "empty"}}).at("selected");
        authoring_command(scene, "component.add",
                          {{"entity", entity}, {"component", "forge.mesh_renderer"}});
        ComponentInspector inspector;
        inspector.edit_property(scene, entity, "forge.mesh_renderer", "mesh",
                                engine_primitive(0).id);
        const auto before_assignment = scene.document();
        const auto assignments = Json::array({{{"slot", "surface"}, {"material", id}}});
        inspector.edit_property(scene, entity, "forge.mesh_renderer", "materials", assignments);
        const auto assigned = scene.document();
        require(assigned != before_assignment && assigned.at("entities")
                                                         .back()
                                                         .at("components")
                                                         .at("forge.mesh_renderer")
                                                         .at("materials") == assignments,
                "Inspector did not assign material through reflected application operation");
        require(scene.undo() && scene.document() == before_assignment && scene.redo() &&
                    scene.document() == assigned,
                "Material assignment did not retain one scene history operation");
        inspector.material_slots = [&](const Json&, Json& value) { return slots.draw(value); };
        ImGui::NewFrame();
        ImGui::Begin("Inspector test");
        inspector.draw(scene, project, entity);
        ImGui::End();
        ImGui::Render();
        require(scene.document() == assigned, "Drawing material slots mutated authored state");
        inspector.edit_property(scene, entity, "forge.mesh_renderer", "materials", Json::array());
        require(scene.document() == before_assignment && scene.undo() &&
                    scene.document() == assigned,
                "Mesh default/Revert assignment lost prior scene intent");
        slots.select(root, catalog, {});
        require(slots.bindings().empty() && !slots.pending(),
                "Cleared selection retained mesh slots");
        {
            const auto authored_before_custom = scene.document();
            auto selected = AssetCatalog::open_project(root);
            SurfaceShaderDefinition definition;
            definition.parameters["tint"] = {MaterialParameterType::LinearColor4,
                                             {.2f, .4f, .8f, 1}};
            const auto shader = AssetId::generate();
            test::publish_surface_fixture(root, selected, shader, definition);
            auto custom_source =
                MaterialDocument::create(project.writer_guard(), "Assets/custom.material.json");
            custom_source->edit(custom_source->revision(), "Select Shader", [&](auto& j) {
                j["version"] = 2;
                j["overrides"]["shader"] = AssetRef<ShaderAsset>{shader};
            });
            custom_source->save();
            MaterialEditor custom_editor;
            std::optional<MaterialResourceData> custom_preview;
            unsigned changes = 0;
            custom_editor.update_preview = [&](auto, auto data, auto) {
                custom_preview = std::move(data);
                ++changes;
            };
            custom_editor.open(project, custom_source->locator());
            const auto pump = [&] {
                custom_editor.poll(project, message);
                ImGui::NewFrame();
                custom_editor.draw(project, false);
                ImGui::Render();
            };
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!custom_preview && std::chrono::steady_clock::now() < deadline) {
                pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            require(custom_preview && custom_preview->surface &&
                        custom_preview->surface->shader.id == shader,
                    "Material editor failed to select a custom Shader asynchronously");
            custom_editor.edit(custom_editor.document()->revision(), "Custom tint", [](auto& j) {
                j["overrides"]["parameters"]["tint"] = {
                    {"type", unsigned(MaterialParameterType::LinearColor4)},
                    {"value", {1, 0, 0, 1}}};
            });
            pump();
            require(custom_preview->values.parameters.at("tint").value[0] == 1 &&
                        custom_editor.can_undo(),
                    "Custom parameter edit did not update preview/history");
            custom_editor.undo();
            pump();
            require(custom_preview->values.parameters.at("tint").value[0] == .2f,
                    "Custom parameter Undo did not restore Shader defaults");
            const auto before_bad = changes;
            custom_editor.edit(custom_editor.document()->revision(), "Wrong type", [](auto& j) {
                j["overrides"]["parameters"]["tint"] = {{"type", 0}, {"value", {.5f}}};
            });
            pump();
            require(changes == before_bad && custom_preview->surface,
                    "Invalid custom parameter type replaced the usable preview");
            require(scene.document() == authored_before_custom,
                    "Custom material source editing changed authored scene state");
        }
        ImGui::DestroyContext();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
