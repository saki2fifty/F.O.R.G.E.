#pragma once
#include "workspace.hpp"
#include <chrono>
void require(bool condition, const char* message);
inline void test_workspace_startup() {
    const auto root = std::filesystem::current_path() /
                      ("workspace-tests-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    } cleanup{root};
    const auto path = root / "workspace.ini", backup = root / "workspace-before-layout-update.ini";
    auto read = [](const std::filesystem::path& file) {
        std::ifstream input(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto fresh = forge::ui::prepare_layout(path);
    require(fresh.save_enabled && fresh.text.empty() && fresh.warning.empty(),
            "Fresh workspace failed");
    const std::string original =
        "[Window][World]\r\nPos=70,80\r\nSize=400,300\r\nCollapsed=0\r\n\r\n[Window][Native]"
        "\r\nPos=30,40\r\nSize=400,300\r\nCollapsed=0\r\n";
    forge::atomic_write(path, original);
    auto first = forge::ui::prepare_layout(path);
    require(first.save_enabled && first.warning.empty() && first.text == read(path) &&
                first.text != original,
            "Real file startup migration failed (reader must close before replacement)");
    require(read(backup) == original, "Workspace backup lost original bytes");
    auto second = forge::ui::prepare_layout(path);
    require(second.save_enabled && second.text == first.text && read(backup) == original,
            "Restart changed migrated layout or backup");
    // Retry with a backup already present, as left behind by the broken Build 12 startup.
    forge::atomic_write(path, original);
    require(forge::ui::prepare_layout(path).save_enabled && read(backup) == original,
            "Build 12 backup prevented recovery");
    forge::atomic_write(path, original);
    std::filesystem::remove(backup);
    std::filesystem::create_directory(backup); // Reproducible write failure on Linux and Windows.
    const auto failed = forge::ui::prepare_layout(path);
    require(!failed.save_enabled && !failed.warning.empty() && failed.text == first.text &&
                read(path) == original,
            "Failed backup did not preserve file and in-memory migrated layout");
    ImGui::CreateContext();
    const auto ini = path.string();
    forge::ui::load_startup_layout(failed, ini.c_str());
    require(ImGui::GetIO().IniFilename == nullptr,
            "Recovery would auto-overwrite original workspace");
    const auto* window = ImGui::FindWindowSettingsByID(ImHashStr("Hierarchy###World"));
    require(window && window->Pos.x == 70 && window->Pos.y == 80,
            "In-memory fallback lost custom layout");
    ImGui::DestroyContext();
    require(read(path) == original, "Shutdown overwrote preserved workspace");
    std::filesystem::remove(backup);
#ifdef _WIN32
    {
        // Match the old startup's still-open std::ifstream. No delete sharing on Windows.
        std::ifstream held(path, std::ios::binary);
        bool blocked = false;
        try {
            forge::atomic_write(path, "must not replace locked file");
        } catch (const std::filesystem::filesystem_error& error) {
            blocked = true;
            require(error.path2() == path && error.code().value() != 0,
                    "Replacement diagnostic lost path or Windows error");
        }
        require(blocked, "Fixture did not reproduce Windows open-reader replacement failure");
        const auto locked = forge::ui::prepare_layout(path);
        require(!locked.save_enabled && !locked.warning.empty() && locked.text == first.text &&
                    read(path) == original,
                "Locked destination did not recover safely");
    }
    require(!std::filesystem::exists(path.string() + ".pending"), "Failed save left staging file");
#endif
    require(forge::ui::prepare_layout(path).save_enabled && read(path) == first.text,
            "Retry after file unlock failed");
}
