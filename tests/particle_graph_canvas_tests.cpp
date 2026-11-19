#include "asset_bytes.hpp"
#include "editor/material_graph_canvas.hpp"
#include "imgui_software_capture.hpp"
#include <fstream>
#include <iostream>
using namespace forge;
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Need Gallery root and particle graph capture directory");
        const auto source = MaterialGraphSource::parse(asset_detail::read_bytes(
            std::filesystem::path(argv[1]) / "Assets/Particles/Graphs/Orbit.graph.shader.json",
            1024 * 1024));
        const auto original = source.document.at("graph");
        auto graph = original;
        test::ImGuiSoftwareCapture capture(1440, 1000);
        ui::style(1);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        MaterialGraphCanvas canvas;
        canvas.particle_scope = true;
        canvas.function_label = [&](GraphFunctionId id) {
            for (const auto& function : graph.at("functions"))
                if (function.at("id").get<GraphFunctionId>() == id)
                    return function.at("label").get<std::string>();
            return std::string("Unavailable function");
        };
        for (unsigned i = 0; i < 4; ++i) {
            capture.new_frame();
            ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1424, 984}, ImGuiCond_Always);
            ImGui::Begin("Particle graph | Orbit fountain");
            canvas.draw(
                graph, 1, [&](std::string, const auto& operation) { operation(graph); }, false);
            ImGui::End();
            capture.render();
        }
        if (graph != original)
            throw std::runtime_error("Native particle canvas display changed the source graph");
        (void)compile_particle_graph(graph);
        std::filesystem::create_directories(argv[2]);
        capture.save(std::filesystem::path(argv[2]) / "particle-graph.png");
        std::cout << "Native particle canvas capture preserved the editable Gallery program\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
