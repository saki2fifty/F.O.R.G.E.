#include "ui_surface_limits.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto actor = authoring_command(scene, "entity.create", {{"name", "Panel"}})
                               .at("selected")
                               .get<std::string>();
        const auto add = [&](const char* component) {
            authoring_command(scene, "component.add",
                              {{"entity", actor}, {"component", component}});
        };
        const auto set = [&](const char* component, const char* field, Json value) {
            authoring_command(scene, "property.set",
                              {{"entity", actor},
                               {"component", component},
                               {"field", field},
                               {"value", std::move(value)}});
        };
        add("forge.mesh_renderer");
        set("forge.mesh_renderer", "mesh", engine_primitive(8).id);
        add("forge.ui_surface_input");
        const auto output = AssetId::generate();
        set("forge.ui_surface_input", "render_texture", output);
        const auto schema = scene.schema();
        const auto declaration = std::find_if(
            schema.at("components").begin(), schema.at("components").end(),
            [](const Json& value) { return value.at("id") == "forge.ui_surface_input"; });
        check(declaration != schema.at("components").end() &&
                  declaration->at("category") == "Runtime UI" &&
                  declaration->at("display_name") == "UI Surface Input" &&
                  declaration->at("optional").get<bool>(),
              "UI Surface Input discovery differs from the shared component owner");
        auto frame = [&] { return extract_render_scene(scene.effective_document()); };
        auto view = frame();
        check(
            view.ui_surfaces.size() == 1 && view.ui_surfaces[0].entity == EntityId::parse(actor) &&
                view.ui_surfaces[0].input.render_texture.id == output &&
                view.meshes.at(view.ui_surfaces[0].mesh_index).entity == view.ui_surfaces[0].entity,
            "UI surface lost typed values or its detached mesh reference");
        const auto original = scene.document();
        set("forge.ui_surface_input", "enabled", false);
        check(frame().ui_surfaces.empty() && scene.undo() && scene.document() == original &&
                  frame().ui_surfaces.size() == 1 && scene.redo() && frame().ui_surfaces.empty(),
              "UI surface enabled history differs from ordinary Scene operations");
        check(scene.undo(), "UI surface history did not restore source");
        set("forge.ui_surface_input", "render_texture", nullptr);
        check(frame().ui_surfaces.empty(), "Unassigned UI surface supplied input");
        check(scene.undo(), "Undo failed");
        add("forge.node_selectability");
        set("forge.node_selectability", "selectable", false);
        check(frame().ui_surfaces.size() == 1 && !frame().meshes[0].selectable,
              "Editor selectability incorrectly disabled game UI input");
        set("forge.mesh_renderer", "visible", false);
        check(frame().ui_surfaces.empty(), "Invisible UI surface supplied input");
        check(scene.undo(), "Undo failed");
        set("forge.mesh_renderer", "mesh", engine_primitive(0).id);
        view = frame();
        check(view.ui_surfaces.empty() &&
                  std::any_of(view.diagnostics.begin(), view.diagnostics.end(),
                              [](const Diagnostic& d) {
                                  return d.category == "render.ui_surface.invalid";
                              }),
              "Unsupported surface geometry was silently interactive");
        check(scene.undo(), "Undo failed");
        auto source = scene.effective_document();
        const auto row = source.at("entities").front();
        source["entities"] = Json::array();
        for (unsigned i = 0; i <= ui_surface_detail::max_surfaces; ++i) {
            auto copy = row;
            copy["id"] = EntityId::generate();
            source["entities"].push_back(std::move(copy));
        }
        std::sort(source["entities"].begin(), source["entities"].end(),
                  [](const Json& a, const Json& b) {
                      return a.at("id").get<std::string>() > b.at("id").get<std::string>();
                  });
        view = extract_render_scene(source);
        check(view.ui_surfaces.size() == ui_surface_detail::max_surfaces &&
                  !view.diagnostics.empty(),
              "Interactive surface admission exceeded its shared count limit");
        for (const auto& surface : view.ui_surfaces)
            check(view.meshes.at(surface.mesh_index).entity == surface.entity,
                  "Surface-to-mesh index changed after detached mesh ordering");
        check(scene.entity(actor).get<UiSurfaceInput>().render_texture.id == output,
              "Reflected surface properties differ from native component storage");
        std::cout << "UI surface metadata, native storage, history, mesh references, visibility "
                     "and bounds passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
