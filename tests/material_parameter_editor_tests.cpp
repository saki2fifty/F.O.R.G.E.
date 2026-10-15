#include "editor/component_inspector.hpp"
#include "editor/mesh_material_inspector.hpp"
#include "material_authoring.hpp"
#include "material_document.hpp"
#include "surface_material_fixture.hpp"
#include <forge/engine_assets.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need object material UI scratch root");
        for (const float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root);
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto entity =
                authoring_command(scene, "entity.create").at("selected").get<std::string>();
            authoring_command(scene, "component.add",
                              {{"entity", entity}, {"component", "forge.mesh_renderer"}});
            ComponentInspector inspector;
            inspector.edit_property(scene, entity, "forge.mesh_renderer", "mesh",
                                    engine_primitive(0).id);
            MeshMaterialInspector materials;
            materials.select(root, std::make_shared<const AssetCatalog>(root), engine_primitive(0));
            Json values = Json::array();
            const auto baseline = scene.document();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1200, 1800};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::style(scale);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            bool live = false;
            Json live_values = Json::array();
            auto frame = [&] {
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1150, 1750}, ImGuiCond_Always);
                ImGui::Begin("Object material values");
                if (materials.draw_parameters(live ? live_values : values) && !live)
                    inspector.edit_property(scene, entity, "forge.mesh_renderer",
                                            "material_parameters", values);
                ImGui::End();
                ImGui::Render();
            };
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                frame();
                require(std::chrono::steady_clock::now() < deadline,
                        "Object material Inspector loading timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (materials.pending() || materials.parameters_pending());
            frame();
            require(scene.document() == baseline && values.empty(),
                    "Drawing async parameters authored an unsolicited override");
            auto click = [&](const std::string& key) {
                const auto box = test::ui_targets.at(key);
                require(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                            box.maximum.y <= box.clip_maximum.y,
                        "Object material control is disabled or clipped: " + key);
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                    (box.minimum.y + box.maximum.y) * .5f);
                frame();
                io.AddMouseButtonEvent(0, true);
                frame();
                io.AddMouseButtonEvent(0, false);
                frame();
            };
            click("object-material:parameter:roughnessFactor");
            click("object-material:add:roughnessFactor");
            require(values.size() == 1 && values[0].at("parameter").at("value")[0] == 1,
                    "Add override did not preserve explicit equal-to-default intent");
            const auto added = scene.document();
            require(scene.undo() && scene.document() == baseline && scene.redo() &&
                        scene.document() == added,
                    "Object value add did not use one Scene Undo/Redo transaction");
            click("object-material:value:roughnessFactor");
            io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            io.AddInputCharactersUTF8("0.25");
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            require(values[0].at("parameter").at("value")[0] == .25 &&
                        scene.entity(entity)
                                .get<MeshRenderer>()
                                .material_parameters[0]
                                .parameter.value[0] == .25f,
                    "Typed Inspector input did not reach native MeshRenderer values");
            const auto valid = values;
            require(!materials.commit_parameter(values, "roughnessFactor",
                                                {MaterialParameterType::Scalar, {2}}) &&
                        values == valid,
                    "Invalid Inspector range partially mutated object values");
            auto extensions = values;
            extensions[0]["future_owner"] = "slot-and-key";
            extensions[0]["parameter"]["future_value"] = true;
            require(materials.commit_parameter(extensions, "roughnessFactor",
                                               {MaterialParameterType::Scalar, {.4f}}) &&
                        extensions[0].at("future_owner") == "slot-and-key" &&
                        extensions[0].at("parameter").at("future_value") == true,
                    "Inspector edit erased unknown row/parameter extension data");
            click("object-material:revert:roughnessFactor");
            require(values.empty() &&
                        scene.entity(entity).get<MeshRenderer>().material_parameters.empty(),
                    "Revert did not remove explicit object intent");
            // The live property caller owns a separate transient document; use
            // the same widgets, then prove no authored history was changed.
            const auto authored = scene.document();
            live = true;
            frame();
            click("object-material:add:roughnessFactor");
            require(live_values.size() == 1 && scene.document() == authored,
                    "Live value widgets dirtied the authored Scene");
            // A real callback through the live Inspector must use that transient
            // command path, not silently fall back to generic vector editing.
            unsigned live_callback = 0;
            inspector.material_parameters = [&](const Json&, Json& incoming) {
                ++live_callback;
                return materials.draw_parameters(incoming);
            };
            auto schema = scene.schema();
            auto types = Json::array();
            for (auto type : schema.at("components"))
                if (type.at("id") == "forge.mesh_renderer") {
                    auto fields = Json::array();
                    for (const auto& field : type.at("fields"))
                        if (field.at("id") == "material_parameters")
                            fields.push_back(field);
                    type["fields"] = std::move(fields);
                    types.push_back(std::move(type));
                }
            schema["components"] = std::move(types);
            ImGui::NewFrame();
            ImGui::Begin("Live property caller");
            inspector.draw_runtime(
                scene.effective_document(), schema, root, entity,
                [](const auto&, const auto&, const auto&) { return true; }, false);
            ImGui::End();
            ImGui::Render();
            require(live_callback == 1 && scene.document() == authored,
                    "Live Inspector did not route material controls to transient values");
            // Bound model controls to fields actually used by that model.
            // Legacy unlit artifacts retain roughness storage for compatibility;
            // presenting that field as a working surface control is misleading.
            materials.select(root, std::make_shared<const AssetCatalog>(root), engine_primitive(0),
                             {{"surface", engine_material(EngineMaterial::LegacyBlockout)}});
            live = false;
            values = Json::array();
            const auto unlit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                frame();
                require(std::chrono::steady_clock::now() < unlit_deadline,
                        "Unlit material definitions timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (materials.pending() || materials.parameters_pending());
            frame();
            require(test::ui_targets.contains("object-material:parameter:baseColorFactor") &&
                        !test::ui_targets.contains("object-material:parameter:roughnessFactor") &&
                        scene.document() == authored,
                    "Unlit Inspector advertised ineffective fields or changed the Scene");
            // Real async material selection with an explicitly CPU-only Shader
            // transport fixture. Its marker bytes never reach a graphics API.
            std::filesystem::create_directories(root / "Assets");
            auto catalog = AssetCatalog::open_project(root);
            const auto shader_id = AssetId::generate();
            SurfaceShaderDefinition definition;
            definition.physically_based = true;
            definition.uv_sets = {0};
            definition.parameters = {
                {"scalar", {MaterialParameterType::Scalar, {1}}},
                {"vector2", {MaterialParameterType::Vector2, {1, 2}}},
                {"vector3", {MaterialParameterType::Vector3, {1, 2, 3}}},
                {"vector4", {MaterialParameterType::Vector4, {1, 2, 3, 4}}},
                {"rgb", {MaterialParameterType::LinearColor3, {.1f, .2f, .3f}}},
                {"rgba", {MaterialParameterType::LinearColor4, {.2f, .3f, .4f, 1}}}};
            definition.labels["rgb"] = "Object color";
            test::publish_surface_fixture(root, catalog, shader_id, definition);
            auto custom =
                MaterialDocument::create(project.writer_guard(), "Assets/custom.material.json");
            custom->edit(custom->revision(), "Choose typed surface", [&](auto& document) {
                document["version"] = 2;
                document["overrides"]["shader"] = AssetRef<ShaderAsset>{shader_id};
            });
            custom->save();
            const auto material_id = custom->source().asset();
            const auto material_source = custom->source().document;
            AssetImportService imports(project.writer_guard(), material_import_registry(),
                                       {"windows", "d3d12", "desktop"}, 1);
            imports.submit(
                imports.prepare(custom->locator(), {}, material_id),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_material_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            require(imports.wait_idle(std::chrono::seconds(30)),
                    "Custom surface material publication timed out");
            const auto receipts = imports.poll();
            require(receipts.size() == 1 && receipts[0].published,
                    receipts.empty() ? "Custom material receipt missing" : receipts[0].diagnostic);
            inspector.edit_property(
                scene, entity, "forge.mesh_renderer", "materials",
                Json::array({{{"slot", "surface"}, {"material", material_id}}}));
            materials.select(root,
                             std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)),
                             engine_primitive(0), {{"surface", {material_id}}});
            values = Json::array();
            const auto custom_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                frame();
                require(std::chrono::steady_clock::now() < custom_deadline,
                        "Typed custom material loading timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (materials.pending() || materials.parameters_pending());
            frame();
            for (const auto& [key, parameter] : definition.parameters) {
                require(test::ui_targets.contains("object-material:parameter:" + key),
                        "Custom surface typed parameter was not exposed");
                auto changed = parameter;
                changed.value[0] += .125f;
                require(materials.commit_parameter(values, key, changed),
                        "Typed custom surface value was rejected");
            }
            inspector.edit_property(scene, entity, "forge.mesh_renderer", "material_parameters",
                                    values);
            require(scene.entity(entity).get<MeshRenderer>().material_parameters.size() == 6,
                    "Custom material kinds did not reach native MeshRenderer storage");
            frame();
            click("object-material:parameter:rgb");
            click("object-material:value:rgb");
            io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            io.AddInputCharactersUTF8("0.75");
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            const auto& native_values =
                scene.entity(entity).get<MeshRenderer>().material_parameters;
            const auto rgb = std::find_if(native_values.begin(), native_values.end(),
                                          [](const auto& item) { return item.key == "rgb"; });
            require(
                rgb != native_values.end() &&
                    rgb->parameter.type == MaterialParameterType::LinearColor3 &&
                    rgb->parameter.value[1] == .75f && rgb->parameter.value[3] == 0,
                "Actual multi-lane input lost custom color semantics or changed unused storage");
            const auto valid_custom = values;
            require(!materials.commit_parameter(
                        values, "rgb", {MaterialParameterType::Vector3, {.1f, .2f, .3f}}) &&
                        values == valid_custom && custom->source().document == material_source,
                    "Custom Inspector confused color/vector types or edited the shared source");
            ImGui::DestroyContext();
        }
        std::cout << "Async object material Inspector input at100/150/200%, typed native values, "
                     "history, invalid retention, extension data and live isolation passed\n";
    } catch (const std::exception& error) {
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
