#include "editor/foliage_surface_brush.hpp"
#include "mesh_scatter_authoring.hpp"
#include <forge/primitive_catalog.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                                .at("selected")
                                .get<EntityId>();
        const auto surface = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                                 .at("selected")
                                 .get<EntityId>();
        detail::MeshScatterSettings settings;
        settings.count = 2;
        settings.radius = .1;
        settings.spacing = 0;
        auto dab = detail::mesh_scatter_arguments(source.str(), settings, "Stroke");
        dab.erase("entity");
        dab.erase("name");
        Json config{{"entity", source}, {"name", "Stroke"}, {"dab", dab}, {"dab_distance", 1.}};
        const auto original_config = config;
        const auto before = scene.document();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigInputTrickleEventQueue = false;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::FoliageSurfaceBrush brush;
        EditorCamera camera;
        EntityId selected = surface;
        bool focused = true, editable = true, occluded = false;
        std::string revision = "immutable-selection", error;
        std::optional<Json> released;
        ImVec2 origin{}, size{600, 400};
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({700, 500});
            ImGui::Begin("Scene brush", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("Surface", size, ImGuiButtonFlags_MouseButtonLeft);
            const bool hover = ImGui::IsItemHovered(), activated = ImGui::IsItemActivated(),
                       active = ImGui::IsItemActive();
            try {
                if (auto value = brush.input(
                        scene, selected, surface, config, camera, origin, size, focused, editable,
                        hover, activated, active,
                        [&](double x, double y) -> std::optional<MeshSurfaceHit> {
                            if (occluded)
                                return {};
                            return MeshSurfaceHit{surface, engine_primitive(0).id, revision, "", 0,
                                                  0,       {x * .05, .5, y * .05}};
                        }))
                    released = std::move(value);
            } catch (const std::exception& failure) {
                error = failure.what();
            }
            brush.overlay(origin, size, hover);
            ImGui::End();
            ImGui::Render();
        };
        auto move = [&](float x) {
            io.AddMousePosEvent(origin.x + x, origin.y + 100);
            frame();
        };
        auto start = [&] {
            error.clear();
            released.reset();
            move(100);
            io.AddMouseButtonEvent(0, true);
            frame();
            check(brush.holding(), "Actual pointer press did not capture foliage brush");
        };
        auto release = [&] {
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        frame();
        frame();
        start();
        move(110); // Below the horizontal dab distance.
        occluded = true;
        move(140);
        occluded = false;
        move(160);
        check(scene.document() == before && !released, "Pointer preview published Scene intent");
        release();
        check(released && released->at("dabs").size() == 2 && error.empty(),
              "Dab distance or occluded sampling changed atomic release");
        const auto placed = authoring_command(scene, "mesh.stroke", *released);
        check(placed.at("created").size() == 4 && scene.undo() && scene.document() == before,
              "Actual pointer stroke did not use one Scene Undo");
        for (unsigned reason = 0; reason < 9; ++reason) {
            config = original_config;
            selected = surface;
            focused = editable = true;
            revision = "immutable-selection";
            camera.yaw = 0;
            start();
            switch (reason) {
            case 0:
                focused = false;
                break;
            case 1:
                editable = false;
                break;
            case 2:
                selected = source;
                break;
            case 3:
                config["dab"]["radius"] = .2;
                break;
            case 4:
                camera.yaw = .2f;
                break;
            case 5:
                size.x -= 10;
                break;
            case 6:
                io.AddKeyEvent(ImGuiKey_Escape, true);
                break;
            case 7:
                revision = "new-publication";
                break;
            case 8:
                authoring_command(scene, "entity.rename",
                                  {{"entity", source}, {"name", "Changed"}});
                break;
            }
            move(160);
            release();
            check(!brush.holding() && !released && !error.empty(),
                  "Changed stroke context published or retained captured intent");
            io.AddKeyEvent(ImGuiKey_Escape, false);
            focused = editable = true;
            selected = surface;
            config = original_config;
            size = {600, 400};
            if (reason == 8)
                check(scene.undo(), "Scene mutation cancellation cleanup failed");
            frame();
            check(scene.document() == before, "Cancelled brush changed authored objects");
        }
        revision = "immutable-selection";
        config["dab"]["count"] = 256;
        start();
        move(160);
        move(220);
        release();
        check(!released && !brush.holding() && !error.empty() && scene.document() == before,
              "Over-budget gesture partially published or remained captured");
        ImGui::DestroyContext();
        std::cout << "Actual foliage pointer capture, sampled preview, atomic release/Undo and "
                     "context/publication/budget cancellation pass\n";
    } catch (const std::exception& error) {
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
