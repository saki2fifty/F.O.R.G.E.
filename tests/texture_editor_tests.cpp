#include "texture_imports.hpp"
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void source(const std::filesystem::path& path) {
    std::vector<unsigned char> bytes(18);
    bytes[2] = 2;
    bytes[12] = 2;
    bytes[14] = 2;
    bytes[16] = 24;
    bytes[17] = 32;
    bytes.insert(bytes.end(), 12, 255);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    require(bool(f), "Cannot write owned TGA");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need worker and scratch root");
        auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        source(root / "Assets/image.tga");
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
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        TextureImportEditor editor(std::filesystem::absolute(argv[1]));
        editor.preview_before_settings = true;
        unsigned preview_calls = 0;
        editor.draw_extension = [&](auto&, bool) {
            require((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0,
                    "Preview navigation was disabled with import settings");
            ++preview_calls;
        };
        std::string message;
        auto frame = [&] {
            editor.poll(document, message);
            ImGui::NewFrame();
            editor.draw(document, false);
            ImGui::Render();
        };
        editor.open(document, "Assets/image.tga");
        for (const auto& problem : context.problems.items())
            std::cerr << problem.text << '\n';
        require(editor.dirty(), "New import is not pending");
        frame();
        require(preview_calls == 1, "Texture preview was omitted or duplicated in its document");
        editor.request_close();
        frame();
        require(ImGui::GetTopMostPopupModal() != nullptr, "Close skipped unpublished import guard");
        editor.request_save();
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (editor.is_open() && std::chrono::steady_clock::now() < until) {
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(!editor.is_open() && !editor.dirty(),
                "Successful import did not finish guarded close");
        frame();
        require(ImGui::GetTopMostPopupModal() == nullptr,
                "Successful guarded close left a modal blocking editor");
        const auto catalog = AssetCatalog::open_project(root);
        require(catalog.records().size() == 1, "Editor import failed to publish");
        const auto id = catalog.records().begin()->first;
        editor.open(document, "Assets/image.tga");
        frame();
        require(!editor.dirty() && editor.is_open(), "Saved import settings reopened dirty");
        require(context.task.id() == "texture_import", "Texture document did not own Save focus");
        editor.request_save();
        frame();
        const auto retry_until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (editor.dirty() && std::chrono::steady_clock::now() < retry_until) {
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(!editor.dirty() && AssetCatalog::open_project(root).records().contains(id),
                "Editor reimport changed identity or stayed busy");
        require(!scene.can_undo(), "Asset publication entered scene Undo");
        editor.request_close();
        frame();
        require(!editor.is_open(), "Clean document close failed");
        ImGui::DestroyContext();
        std::cout << "Texture editor actual import, guarded close, focus, reimport and history "
                     "boundary passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
