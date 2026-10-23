#pragma once
#include <filesystem>
namespace forge::ui {
// Ephemeral connection grants, never project settings or authored state.
struct AutomationPermissions {
    bool assets = false, code = false, execute = false, export_game = false;
    std::filesystem::path import_root, export_root, workspace_root;
};
} // namespace forge::ui
