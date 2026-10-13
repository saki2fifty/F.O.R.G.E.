#include "editor/editable_mesh_editor.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
EditableMeshSource strip() {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    const std::array<std::array<double, 3>, 6> points{
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {2, 0, 0}, {2, 1, 0}}};
    source.document["vertices"] = Json::array();
    for (unsigned i = 0; i < 6; ++i)
        source.document["vertices"].push_back({{"id", i + 1}, {"position", points[i]}});
    source.document["faces"] = Json::array();
    for (unsigned f = 0; f < 2; ++f) {
        Json corners = Json::array();
        const std::array<std::array<unsigned, 4>, 2> faces{{{1, 2, 3, 4}, {2, 5, 6, 3}}};
        for (auto vertex : faces[f])
            corners.push_back(
                {{"vertex", vertex}, {"uv", {points[vertex - 1][0], points[vertex - 1][1]}}});
        source.document["faces"].push_back({{"id", f + 7}, {"corners", corners}});
    }
    source.document["next_id"] = 9;
    source.validate();
    return source;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need UV UI scratch directory");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            const auto source = strip();
            atomic_write(root / "Assets/Strip.mesh.json", source.document.dump(2));
            struct Cleanup {
                std::filesystem::path path;
                ~Cleanup() {
                    std::error_code ec;
                    std::filesystem::remove_all(path, ec);
                }
            } cleanup{root};
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto scene_before = scene.document();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.DisplaySize = {1600, 900};
            io.DeltaTime = 1.f / 60;
            io.FontGlobalScale = scale;
            ui::interface_scale = scale;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            EditableMeshEditor editor;
            editor.open(project, "Assets/Strip.mesh.json");
            std::string message;
            bool locked = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1600, 900}, ImGuiCond_Always);
                editor.draw(project, locked);
                ImGui::Render();
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto target = [&](const std::string& name) {
                frame();
                require(test::ui_targets.contains(name), "Missing " + name);
                auto box = test::ui_targets.at(name);
                require(box.enabled, "Disabled " + name);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Editable Mesh");
                    require(window, "Mesh document missing");
                    ImGui::SetScrollY(window,
                                      window->Scroll.y + (box.minimum.y + box.maximum.y -
                                                          box.clip_minimum.y - box.clip_maximum.y) /
                                                             2);
                    frame();
                    frame();
                    box = test::ui_targets.at(name);
                }
                require(box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y,
                        "Clipped " + name);
                return box;
            };
            auto click = [&](const std::string& name) {
                const auto box = target(name);
                const ImVec2 point{(box.minimum.x + box.maximum.x) / 2,
                                   (box.minimum.y + box.maximum.y) / 2};
                mouse(point, false);
                mouse(point, true);
                mouse(point, false);
                frame();
            };
            auto uv_point = [&](double u, double v) {
                const auto box = target("mesh:uv_canvas");
                const float base = std::max(
                    16.f, std::min(box.maximum.x - box.minimum.x, box.maximum.y - box.minimum.y) -
                              36 * scale);
                return ImVec2{(box.minimum.x + box.maximum.x) / 2 + float(u - .5) * base,
                              (box.minimum.y + box.maximum.y) / 2 + float(v - .5) * base};
            };
            frame();
            frame();
            click("mesh:uv_tab");
            frame();
            require(test::ui_targets.contains("mesh:uv_corner:7:0"),
                    "UV workspace did not select the source face");
            const auto before = editor.document()->source().document;
            click("button:Select island");
            click("button:Frame tile");
            const auto canvas = target("mesh:uv_canvas");
            const auto base = std::max(16.f, std::min(canvas.maximum.x - canvas.minimum.x,
                                                      canvas.maximum.y - canvas.minimum.y) -
                                                 36 * scale);
            auto start = uv_point(0, 0), finish = ImVec2{start.x + 40, start.y + 20};
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            require(editor.document()->source().document == before,
                    "UV drag authored before release");
            frame(); // Render the evaluated candidate; source remains unchanged.
            auto gold_near = [&](ImVec2 point) {
                unsigned count = 0;
                for (const auto* list : ImGui::GetDrawData()->CmdLists)
                    for (const auto& vertex : list->VtxBuffer)
                        if (vertex.col == IM_COL32(255, 226, 136, 255) &&
                            std::abs(vertex.pos.x - point.x) < 9 * scale &&
                            std::abs(vertex.pos.y - point.y) < 9 * scale)
                            ++count;
                return count;
            };
            require(gold_near(finish) > 0 && gold_near(start) == 0,
                    "Held UV preview did not draw the translated selected corner");

            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            mouse(finish, false);
            frame();
            require(editor.document()->source().document == before, "Escape committed UV preview");
            start = uv_point(0, 0);
            finish = {start.x + 40, start.y + 20};
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            mouse(finish, false);
            frame();
            const auto moved = editor.document()->source().document;
            const auto du = (std::floor(finish.x) - std::floor(start.x)) / base,
                       dv = (std::floor(finish.y) - std::floor(start.y)) / base;
            for (unsigned face = 0; face < 2; ++face)
                for (unsigned c = 0; c < 4; ++c) {
                    require(std::abs(moved["faces"][face]["corners"][c]["uv"][0].get<double>() -
                                     before["faces"][face]["corners"][c]["uv"][0].get<double>() -
                                     du) < 1e-5,
                            "Island move changed U incorrectly");
                    require(std::abs(moved["faces"][face]["corners"][c]["uv"][1].get<double>() -
                                     before["faces"][face]["corners"][c]["uv"][1].get<double>() -
                                     dv) < 1e-5,
                            "Island move changed V incorrectly");
                }
            require(editor.document()->source().uv_island(7).size() == 8,
                    "Drag split the shared UV edge");
            click("button:Undo");
            require(editor.document()->source().document == before,
                    "UV drag was not one Undo step");
            click("button:Redo");
            require(editor.document()->source().document == moved, "UV Redo lost shared mapping");
            click("button:Mirror U");
            const auto mirrored = editor.document()->source().document;
            require(mirrored != moved, "Mirror action omitted UV change");
            click("button:Undo");
            require(editor.document()->source().document == moved,
                    "Mirror did not form one history step");
            click("button:Fit selected UVs");
            const auto fitted = editor.document()->source().document;
            for (const auto& face : fitted["faces"])
                for (const auto& c : face["corners"])
                    for (const auto& value : c["uv"])
                        require(value.get<double>() >= .019 && value.get<double>() <= .981,
                                "Fit action exceeded its padded tile");
            require(editor.document()->source().uv_island(7).size() == 8,
                    "Fit split the shared edge");
            click("button:Frame selection");
            click("button:Frame tile");
            const auto revision = editor.document()->revision();
            const auto saved = fitted;
            auto mid = uv_point(.5, .5);
            mouse(mid, false);
            frame();
            const auto scroll = ImGui::FindWindowByName("###Editable Mesh")->Scroll.y;
            io.AddMouseWheelEvent(0, 1);
            frame();
            frame();
            require(ImGui::FindWindowByName("###Editable Mesh")->Scroll.y == scroll,
                    "Canvas zoom scrolled the document");
            io.AddMouseButtonEvent(2, true);
            frame();
            io.AddMousePosEvent(mid.x + 50, mid.y + 20);
            frame();
            io.AddMouseButtonEvent(2, false);
            frame();
            require(editor.document()->revision() == revision &&
                        editor.document()->source().document == saved,
                    "UV navigation authored data");
            click("button:Frame tile");
            // Out-of-profile pointer movement must reject its scalar preview and release.
            click("button:Frame tile");
            start = uv_point(.02, .26);
            finish = {start.x + 100000000.f, start.y};
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            frame();
            mouse(finish, false);
            frame();
            require(editor.document()->source().document == saved &&
                        editor.document()->revision() == revision,
                    "Invalid derived UV preview changed source/history");
            // Clear via empty canvas, then Ctrl-select two exterior corners.
            click("button:Frame tile");
            auto blank = uv_point(.9, .9);
            mouse(blank, false);
            mouse(blank, true);
            mouse(blank, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            frame();
            click("mesh:uv_corner:7:0");
            click("mesh:uv_corner:7:3");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            const auto sparse_before = editor.document()->source().document;
            const auto sparse_canvas = target("mesh:uv_canvas");
            const auto sparse_base =
                std::max(16.f, std::min(sparse_canvas.maximum.x - sparse_canvas.minimum.x,
                                        sparse_canvas.maximum.y - sparse_canvas.minimum.y) -
                                   36 * scale);
            start = uv_point(sparse_before["faces"][0]["corners"][0]["uv"][0].get<double>(),
                             sparse_before["faces"][0]["corners"][0]["uv"][1].get<double>());
            finish = {start.x + 30, start.y + 15};
            mouse(start, false);
            mouse(start, true);
            // Deliberately place the final movement in the release event.
            mouse(finish, false);
            frame();
            const auto sparse = editor.document()->source().document;
            const auto shift_u = (std::floor(finish.x) - std::floor(start.x)) / sparse_base;
            require(sparse != sparse_before, "Release-only UV movement was lost");
            for (unsigned f = 0; f < 2; ++f)
                for (unsigned c = 0; c < 4; ++c) {
                    if (f == 0 && (c == 0 || c == 3))
                        require(std::abs(
                                    sparse["faces"][f]["corners"][c]["uv"][0].get<double>() -
                                    sparse_before["faces"][f]["corners"][c]["uv"][0].get<double>() -
                                    shift_u) < 1e-5,
                                "Ctrl-selected corner moved incorrectly at scale " +
                                    std::to_string(scale) + " corner " + std::to_string(c) +
                                    " actual U " +
                                    std::to_string(
                                        sparse["faces"][f]["corners"][c]["uv"][0].get<double>()) +
                                    " prior U " +
                                    std::to_string(sparse_before["faces"][f]["corners"][c]["uv"][0]
                                                       .get<double>()) +
                                    " expected shift " + std::to_string(shift_u));
                    else
                        require(sparse["faces"][f]["corners"][c] ==
                                    sparse_before["faces"][f]["corners"][c],
                                "Ctrl selection moved an unselected UV corner");
                }
            click("button:Undo");
            require(editor.document()->source().document == saved, "Sparse UV Undo differs");
            click("button:Select island");
            start = uv_point(.02, .26);
            finish = {start.x + 40, start.y + 20};
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            locked = true;
            frame();
            mouse(finish, false);
            frame();
            locked = false;
            frame();
            require(editor.document()->source().document == saved,
                    "Becoming locked during UV drag committed its candidate");
            start = uv_point(.02, .26);
            finish = {start.x + 40, start.y + 20};
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            mouse(start, false);
            frame();
            require(editor.document()->source().document == saved,
                    "Returning UV drag to its start committed stale movement");
            auto key = [&](ImGuiKey k) {
                io.AddKeyEvent(k, true);
                frame();
                io.AddKeyEvent(k, false);
                frame();
            };
            auto type_first = [&](const std::string& name, const char* value) {
                const auto box = target(name);
                const ImVec2 p{box.minimum.x + 20 * scale, (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                frame();
                key(ImGuiKey_A);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
                io.AddInputCharactersUTF8(value);
                frame();
                key(ImGuiKey_Enter);
            };
            type_first("mesh:uv_move", "0.25");
            click("mesh:uv_preview");
            require(editor.document()->source().document == saved,
                    "Numeric preview authored source");
            click("button:Cancel UV preview");
            require(editor.document()->source().document == saved,
                    "Cancel authored numeric preview");
            type_first("mesh:uv_scale", "0");
            const auto invalid_revision = editor.document()->revision();
            click("button:Apply UV transform");
            require(editor.document()->revision() == invalid_revision &&
                        editor.document()->source().document == saved,
                    "Invalid numeric UV scale entered source history");
            click("button:Cancel UV preview");
            // Exercise region picking through the actual combo and canvas.
            for (bool whole_island : {false, true}) {
                click("mesh:uv_mode");
                key(ImGuiKey_Home);
                key(ImGuiKey_DownArrow);
                if (whole_island)
                    key(ImGuiKey_DownArrow);
                key(ImGuiKey_Enter);
                const auto region_canvas = target("mesh:uv_canvas");
                const auto region_base =
                    std::max(16.f, std::min(region_canvas.maximum.x - region_canvas.minimum.x,
                                            region_canvas.maximum.y - region_canvas.minimum.y) -
                                       36 * scale);
                start = uv_point(.2, .5);
                finish = {start.x + 25, start.y + 10};
                mouse(start, false);
                mouse(start, true);
                mouse(finish, true);
                mouse(finish, false);
                frame();
                const auto region = editor.document()->source().document;
                const auto delta = (std::floor(finish.x) - std::floor(start.x)) / region_base;
                require(region != saved, "Region UV pick/drag did not author");
                for (unsigned f = 0; f < 2; ++f)
                    for (unsigned c = 0; c < 4; ++c)
                        require(std::abs(region["faces"][f]["corners"][c]["uv"][0].get<double>() -
                                         saved["faces"][f]["corners"][c]["uv"][0].get<double>() -
                                         (f == 0 || whole_island ? delta : 0)) < 1e-5,
                                "Face/island canvas scope differs");
                click("button:Undo");
                require(editor.document()->source().document == saved, "Region Undo differs");
            }
            locked = true;
            frame();
            start = uv_point(.02, .26);
            finish = {start.x + 40, start.y + 20};
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            mouse(finish, false);
            frame();
            require(editor.document()->source().document == saved,
                    "Locked UV canvas authored source");
            locked = false;
            frame();
            click("button:Save & Publish");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                frame();
            }
            require(!editor.pending() && !editor.dirty(), "UV save/publication failed");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Published UV document did not close");
            editor.open(project, "Assets/Strip.mesh.json");
            frame();
            require(editor.document()->source().document == saved,
                    "UV reopen lost authored mapping");
            require(scene.document() == scene_before, "UV tools changed Scene history/source");
            test::observe_ui = false;
            ImGui::DestroyContext();
        }
        ui::interface_scale = 1;
        std::cout << "UV island move/cancel/history/mirror/fit/navigation/lock/publication/reopen "
                     "at65/100/200% passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
