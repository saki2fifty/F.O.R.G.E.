#pragma once
#include "files.hpp"
#include "search.hpp"
#include <algorithm>
#include <cctype>
namespace forge {
inline std::vector<std::filesystem::path> scene_files(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    if (error)
        throw std::runtime_error("Cannot read project folder: " + error.message());
    unsigned visited = 0;
    std::uintmax_t scanned_bytes = 0;
    for (; it != end; it.increment(error)) {
        if (++visited > 10000)
            throw std::runtime_error("Scene scan exceeds 10000 entries; use File > Open scene");
        if (error)
            throw std::runtime_error("Scene scan failed: " + error.message());
        const auto& entry = *it;
        const auto name = entry.path().filename();
        if (entry.is_symlink()) {
            it.disable_recursion_pending();
            continue;
        }
        if (entry.is_directory()) {
            if (name == ".forge" || name == ".git" || it.depth() >= 16)
                it.disable_recursion_pending();
            continue;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".json" &&
            name != "forge.project.json") {
            // Recognize legacy/custom .json scenes by structure, not just the extension.
            const auto bytes = entry.file_size();
            scanned_bytes += bytes;
            if (scanned_bytes > 64 * 1024 * 1024)
                throw std::runtime_error("Scene scan exceeds 64 MiB; use File > Open scene");
            if (bytes > 8 * 1024 * 1024)
                continue;
            try {
                const auto candidate = read_json(entry.path());
                if (!candidate.is_object() || !candidate.contains("version") ||
                    !candidate.contains("entities") || !candidate.at("entities").is_array())
                    continue;
            } catch (const std::exception&) {
                continue;
            }
            result.push_back(entry.path().lexically_relative(root));
            if (result.size() >= 4096)
                throw std::runtime_error(
                    "Project has more than 4096 JSON files; use File > Open scene");
        }
    }
    if (error)
        throw std::runtime_error("Scene scan failed: " + error.message());
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return search_key(path_text(a)) < search_key(path_text(b));
    });
    return result;
}
class ContentBrowser {
  public:
    void draw(EditorFiles& files, bool* open = nullptr,
              const std::function<void()>& prefab_controls = {}) {
        if (!ImGui::Begin("Content", open)) {
            ImGui::End();
            return;
        }
        if (prefab_controls) {
            const bool show = ImGui::CollapsingHeader("Prefab assets");
            ui::help("Create, edit and instantiate reusable prefab assets.");
            if (show)
                prefab_controls();
        }
        ui::heading("Project scenes",
                    "Browse recognized scene documents within the current project. "
                    "Open validates scene contents.");
        ImGui::Text("Project: %s", files.document.name().c_str());
        ui::help(path_text(files.document.project()).c_str());
        if (root_ != files.document.project()) {
            root_ = files.document.project();
            selected_.clear();
            paths_.clear();
            refresh_ = true;
        }
        if (ui::button(
                "Refresh",
                "Scan project folders again after files are added or renamed outside FORGE."))
            refresh_ = true;
        ImGui::SameLine();
        if (ui::button("New scene",
                       "Create an unsaved empty scene; Save As chooses its project filename."))
            files.request({EditorFiles::Command::NewScene, {}, {}});
        if (refresh_ || SDL_GetTicks() - refreshed_ > 5000) {
            try {
                paths_ = scene_files(root_);
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
            refresh_ = false;
            refreshed_ = SDL_GetTicks();
        }
        ImGui::InputTextWithHint("##scene-filter", "Filter scene paths...", filter_,
                                 sizeof(filter_));
        ui::help(
            "Filter project-relative JSON filenames. Matching is case-insensitive for ASCII text.");
        const auto needle = search_key(filter_);
        ImGui::BeginChild(
            "##scene-files",
            {0, std::max(60.0f, ImGui::GetContentRegionAvail().y - 100 * ui::interface_scale)},
            ImGuiChildFlags_Borders);
        for (const auto& path : paths_) {
            const auto text = path_text(path);
            if (!needle.empty() && search_key(text).find(needle) == std::string::npos)
                continue;
            if (ImGui::Selectable(text.c_str(), selected_ == path,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_ = path;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    files.request({EditorFiles::Command::OpenScene, root_ / path, {}});
            }
            ui::help("Double-click to open this scene through the unsaved-change guard. Non-scene "
                     "JSON is rejected without replacing your scene.");
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(selected_.empty() || files.busy());
        if (ui::button("Open selected",
                       "Open the selected scene. Unsaved edits are resolved first."))
            files.request({EditorFiles::Command::OpenScene, root_ / selected_, {}});
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", error_.empty()
                                     ? "Scenes only. Asset importing is not available yet."
                                     : error_.c_str());
        ui::help("The browser skips .forge, .git, symbolic links, and folders deeper than 16 "
                 "levels. It refreshes every five seconds while visible.");
        ImGui::End();
    }

  private:
    std::filesystem::path root_, selected_;
    std::vector<std::filesystem::path> paths_;
    std::string error_;
    char filter_[256]{};
    bool refresh_ = true;
    Uint64 refreshed_ = 0;
};
} // namespace forge
