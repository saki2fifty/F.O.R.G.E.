#include "shader_diligent.hpp"
#include "shader_imports.hpp"
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    require(bool(out.write(bytes.data(), std::streamsize(bytes.size()))) && bool(out.flush()),
            "Shader UI fixture write failed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need shader worker and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Shaders");
        const auto id = AssetId::generate();
        write(
            root / "surface.shader.json",
            Json{{"format", "forge.shader"},
                 {"version", 1},
                 {"asset_id", id},
                 {"source_root", "Shaders"},
                 {"stages",
                  Json::array({{{"stage", "vertex"}, {"source", "surface.hlsl"}, {"entry", "vs"}},
                               {{"stage", "pixel"}, {"source", "surface.hlsl"}, {"entry", "ps"}}})}}
                .dump());
        write(root / "Shaders/surface.hlsl",
              "float4 vs(uint id:SV_VertexID):SV_POSITION{return float4(id,0,0,1);}\nfloat4 "
              "ps():SV_TARGET{return 1;}\n");
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument document(scene);
        document.open_project(root, true);
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {900, 800};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        ShaderImportEditor editor(std::filesystem::absolute(argv[1]), [] {
            return asset_detail::ShaderCompilerProfile{
                asset_detail::diligent_shader_compiler_digest(),
                asset_detail::diligent_shader_compiler_debug()};
        });
        std::string message;
        auto frame = [&] {
            editor.poll(document, message);
            ImGui::NewFrame();
            editor.draw(document, false);
            ImGui::Render();
        };
        auto finish = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            do {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (editor.pending() && std::chrono::steady_clock::now() < deadline);
            require(!editor.pending(), "Shader UI compilation timed out");
        };
        editor.open(document, "surface.shader.json");
        require(editor.selected_asset() == id && editor.dirty(),
                "Shader UI lost authored first-publication identity");
        editor.request_close();
        frame();
        require(ImGui::GetTopMostPopupModal() != nullptr,
                "Shader close omitted pending source guard");
        editor.request_save();
        finish();
        frame();
        require(!editor.is_open() && !editor.dirty(),
                "Compiled shader did not finish guarded close");
        const auto good = AssetCatalog::open_project(root).document();
        require(AssetCatalog::open_project(root).records().contains(id),
                "Shader UI did not publish source UUID");
        editor.open(document, "surface.shader.json");
        write(root / "Shaders/surface.hlsl", "invalid shader @");
        editor.request_save();
        finish();
        require(AssetCatalog::open_project(root).document() == good,
                "Failed shader UI compile replaced last-good asset");
        require(!context.problems.items().empty(),
                "Shader compilation error missing from Problems");
        const auto& problem = context.problems.items().back();
        std::cerr << "Shader diagnostic location: " << problem.source << ':' << problem.line << ':'
                  << problem.column << '\n'
                  << problem.text << '\n';
        require(problem.asset == id && problem.source == "Shaders/surface.hlsl" &&
                    problem.line == 1 && problem.column > 0 && problem.source_navigation,
                "Shader compile failure did not navigate to its authored asset and HLSL location");
        require(!document.dirty(), "Shader UI compilation dirtied authored scene");
        ImGui::DestroyContext();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
