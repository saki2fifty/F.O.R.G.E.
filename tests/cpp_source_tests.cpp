#include "../src/gameplay_source_identity.hpp"
#include "cpp_source_files.hpp"
#include <TextEditor.h>
#include <chrono>
#include <cstdint>
#include <imgui.h>
#include <iostream>

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char* message) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, message);
}
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const auto root = std::filesystem::absolute(argv[1]);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Native");
    try {
        using namespace forge::ui;
        forge::asset_storage::replace(root / "Native/main.cpp", "int main() { return 0; }\r\n");
        auto baseline = read_cpp_source(cpp_source_path(root, "Native/main.cpp"));
        save_cpp_source(root, "Native/main.cpp", baseline, "int main() { return 1; }\n");
        rejects([&] { save_cpp_source(root, "Native/main.cpp", baseline, "lost update"); },
                "External source edit overwritten");
        rejects([&] { cpp_source_path(root, "../escape.cpp"); }, "Source escaped project");
        rejects([&] { cpp_source_path(root, "Native/Builds/kit/source.cpp"); },
                "Deployment source admitted");
        rejects([&] { validate_cpp_text(std::string("a\0b", 3)); }, "NUL accepted");
        rejects([&] { validate_cpp_text(std::string("\xc0\x80", 2)); }, "Invalid UTF8 accepted");
        rejects([&] { validate_cpp_text(std::string(max_cpp_source_bytes + 1, 'x')); },
                "Oversized source accepted");
        const auto log =
            forge::path_utf8(root / "Native/main.cpp") + "(12,4): error C2143: syntax error\n" +
            forge::path_utf8(root / "Native/main.cpp") + ":8:3: warning: example\n" +
            "/external/other.cpp:1:1: error: no\nNative/main.cpp(21474836480): error: overflow\n";
        auto diagnostics = cpp_diagnostics(root, log);
        require(diagnostics.size() == 2 && diagnostics[0].line == 12 &&
                    diagnostics[0].column == 4 && diagnostics[1].line == 8 &&
                    diagnostics[1].column == 3,
                "Compiler diagnostic routing incorrect");
        forge::asset_storage::replace(root / "Native/forge.sdk-project.json", "{}");
        forge::asset_storage::replace(
            root / "Native/CMakeLists.txt",
            "add_library(gameplay MODULE gameplay.cpp)\ninclude(forge.sources.cmake OPTIONAL)\n");
        create_cpp_source(root, "behavior.cpp");
        require(read_cpp_source(root / "Native/forge.sources.cmake").find("behavior.cpp") !=
                    std::string::npos,
                "New CPP not registered");
        rejects([&] { create_cpp_source(root, "behavior.cpp"); }, "Existing source overwritten");
        rejects([&] { create_cpp_source(root, "x;evil.cpp"); }, "CMake source injection accepted");
        forge::asset_storage::replace(
            root / "Native/forge.registration.hpp",
            "#pragma once\n// FORGE_COMPONENT_INCLUDES\n// FORGE_SYSTEM_DECLARATIONS\n"
            "inline int forge_register_components(const ForgeSdkWorldV1* host, char* error, "
            "unsigned capacity) {\n// FORGE_COMPONENT_CALLS\nreturn 1; }\n"
            "inline void forge_register_systems(const ForgeSdkWorldV1* host) {\n"
            "// FORGE_SYSTEM_CALLS\n}\n");
        require(create_cpp_component(root, "Rotator") == "Native/Components/Rotator.hpp",
                "Component source was not created");
        require(create_cpp_system(root, "RotationSystem", "Rotator") ==
                    "Native/Systems/RotationSystem.cpp",
                "System source was not created");
        const auto component = read_cpp_source(root / "Native/Components/Rotator.hpp");
        const auto system = read_cpp_source(root / "Native/Systems/RotationSystem.cpp");
        const auto registry = read_cpp_source(root / "Native/forge.registration.hpp");
        require(component.find(R"RAW(R"({"speed":90})")RAW") != std::string::npos &&
                    component.find(".member<float>(\"speed\")") != std::string::npos,
                "Generated reflected component is invalid");
        require(system.find(".kind(host->fixed_phase)") != std::string::npos &&
                    system.find("world.system<const Rotator>") != std::string::npos &&
                    system.find("entity.has<forge::LocalRotation>()") != std::string::npos &&
                    system.find("entity.set<forge::LocalRotation>") != std::string::npos,
                "Generated Flecs system is invalid");
        require(registry.find("#include \"Components/Rotator.hpp\"") != std::string::npos &&
                    registry.find("forge_register_system_RotationSystem(host)") !=
                        std::string::npos,
                "Generated source is not registered");
        require(managed_component_definition(root, "project.rotator") ==
                    "Native/Components/Rotator.hpp",
                "Component definition provenance missing");
        require(!managed_component_definition(root, "project.unknown"),
                "Unknown component invented a source mapping");
        const auto systems = managed_system_sources(root);
        require(systems.size() == 1 && systems.front().name == "RotationSystem" &&
                    systems.front().component == "Rotator" &&
                    systems.front().source == "Native/Systems/RotationSystem.cpp",
                "Registered system inspection is incorrect");
        require(cpp_project_sources(root).size() >= 5, "Gameplay source browser missed files");
        rejects([&] { create_cpp_component(root, "Rotator"); }, "Component overwritten");
        rejects([&] { create_cpp_system(root, "RotationSystem", "Rotator"); },
                "System overwritten");
        const auto original_identity = forge::gameplay_source_digest(root);
        std::filesystem::create_directories(root / "Native/Builds");
        forge::asset_storage::replace(root / "Native/Builds/ignored.cpp", "generated");
        require(forge::gameplay_source_digest(root) == original_identity,
                "Generated deployment affected source identity");
        forge::asset_storage::replace(root / "Native/behavior.cpp", "int changed = 1;\n");
        require(forge::gameplay_source_digest(root) != original_identity,
                "Saved source change did not invalidate gameplay build");
        const auto digest = forge::gameplay_source_digest(root);
        const auto id = forge::gameplay_build_identity(digest, std::string(64, 'a'));
        const nlohmann::json settings = {{"modules",
                                          {{{"id", "project.gameplay"},
                                            {"fingerprint", std::string(64, 'a')},
                                            {"source_identity", id}}}}};
        require(forge::gameplay_source_current(root, settings), "Current build not recognized");
        forge::asset_storage::replace(root / "Native/behavior.cpp", "int changed = 2;\n");
        require(!forge::gameplay_source_current(root, settings),
                "Stale last-good module reported as current");
        auto* context = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.0f / 60;
        unsigned char* pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        TextEditor editor;
        editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        editor.SetText("int value = 0;\n");
        editor.SetCursorPosition({1, 0});
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::Begin("Code");
            editor.Render("Source", {700, 450});
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        io.AddInputCharactersUTF8("// hello");
        frame();
        require(editor.GetText().find("// hello") != std::string::npos && editor.CanUndo(),
                "Actual source typing/history failed");
        editor.Undo();
        require(editor.CanRedo(), "Source Redo missing");
        editor.Redo();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        editor.Undo();
        editor.Redo();
        frame(); // Upstream end-of-file newline/history regression.
        editor.SetReadOnly(true);
        const auto text = editor.GetText();
        io.AddInputCharactersUTF8("blocked");
        frame();
        require(editor.GetText() == text, "Read-only source modified");
        editor.SetText("// UTF-8: café\n");
        frame();
        std::string large;
        for (int i = 0; i < 10000; ++i)
            large += "int sample = 42; // value\n";
        editor.SetReadOnly(false);
        editor.SetText(large);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i)
            frame();
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::cout << "Pinned widget 250KB/10000 lines, 20 headless UI frames: " << ms << " ms\n";
        ImGui::DestroyContext(context);
        std::cout << "C++ source IO, registration, diagnostics and actual editing/history checks "
                     "passed\n";
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
