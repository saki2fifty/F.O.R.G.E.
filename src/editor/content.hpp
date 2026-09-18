#pragma once
#include "files.hpp"
#include "help.hpp"
#include "property_drawer.hpp"
#include "search.hpp"
#include <algorithm>
#include <cctype>
#include <set>
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
    const AssetRecord* record(AssetId id) const {
        if (!catalog_)
            return nullptr;
        const auto it = catalog_->records().find(id);
        return it == catalog_->records().end() ? nullptr : &it->second;
    }
    void refresh(EditorFiles& files) {
        auto candidate = AssetCatalog::open_project(files.document.project());
        // Scenes already carry their AssetId. Use the same catalog abstraction for discovery;
        // browsing never creates a replacement identity or writes a second asset database.
        for (const auto& path : scene_files(files.document.project())) {
            const auto doc = read_json(files.document.project() / path);
            if (!doc.contains("asset_id"))
                continue;
            const auto id = doc.at("asset_id").get<AssetId>();
            if (!candidate.records().contains(id))
                candidate.add_scene(path);
        }
        catalog_ = std::move(candidate);
        refreshed_ = SDL_GetTicks();
    }
    void inspect(EditorFiles& files, ui::EditorSelection& selection,
                 const std::function<void(AssetId)>& open_prefab = {}) {
        const auto* asset = record(selection.asset());
        if (!asset) {
            ImGui::TextWrapped(
                "This asset is no longer available. Refresh Content or select another asset.");
            if (ui::button("Clear selection", "Clear the missing asset selection."))
                selection.clear();
            return;
        }
        ImGui::TextWrapped("Asset: %s", asset_display(*asset).c_str());
        ui::help("Asset inspection does not edit an entity. Select an entity to return to "
                 "component properties.");
        ImGui::Text("Type: %s", asset->type.c_str());
        ImGui::TextWrapped("Source: %s", path_text(asset->source).c_str());
        auto resolution = catalog_->resolve(asset->id, asset->type);
        if (resolution.state != AssetState::Available)
            ui::field_error(resolution.diagnostic);
        if (asset->type == "scene" &&
            ui::button("Open scene", "Open through the scene and draft save guards."))
            files.request(
                {EditorFiles::Command::OpenScene, files.document.project() / asset->source, {}});
        if (asset->type == "prefab" && open_prefab &&
            ui::button("Edit prefab source", "Open this independent prefab source document."))
            open_prefab(asset->id);
        if (ui::button("Reveal source folder",
                       "Open the containing folder in your operating system."))
            SDL_OpenURL(ui::local_file_url((files.document.project() / asset->source).parent_path())
                            .c_str());
        if (ImGui::TreeNode("Asset details")) {
            ImGui::TextWrapped("AssetId: %s", asset->id.str().c_str());
            ImGui::Text("Dependencies: %zu", asset->dependencies.size());
            ImGui::TextWrapped("%s", asset->metadata.dump(2).c_str());
            ImGui::TreePop();
        }
        ui::help("Advanced identity and converter/dependency metadata. AssetIds remain unchanged "
                 "by selecting or inspecting.");
    }
    void draw(EditorFiles& files, bool* open = nullptr,
              const std::function<void()>& prefab_controls = {},
              const std::function<void()>& asset_controls = {}, bool locked = false) {
        if (!ImGui::Begin("Content", open)) {
            ImGui::End();
            return;
        }
        auto& selection = ui::editor_context ? ui::editor_context->selection : fallback_;
        if (ui::editor_context)
            ui::editor_context->task.focus(ui::DocumentTask::Scene);
        if (root_ != files.document.project()) {
            root_ = files.document.project();
            catalog_.reset();
            refreshed_ = 0;
        }
        bool rescan = !catalog_ || SDL_GetTicks() - refreshed_ > 5000;
        if (ui::button("Create / Register", "Create or register supported project assets. These "
                                            "operations are separate from scene Undo."))
            ImGui::OpenPopup("Asset operations");
        ImGui::SameLine();
        if (ui::button("Refresh",
                       "Refresh registered assets and discover saved scene documents.")) {
            rescan = true;
            error_.clear();
        }
        if (ImGui::BeginPopup("Asset operations")) {
            ImGui::BeginDisabled(locked);
            if (ui::button("New scene", "Create an empty scene through the unsaved-change guard."))
                files.request({EditorFiles::Command::NewScene, {}, {}});
            if (ImGui::CollapsingHeader("Audio / Register WAV")) {
                ImGui::InputText("Project WAV path", wav_, sizeof(wav_));
                ui::help("Path relative to this project. Copy your WAV into Assets first; "
                         "registration does not transcode or copy files.");
                if (ui::button("Register WAV", "Register the WAV with an AssetId, then assign it "
                                               "through an Audio Source field.")) {
                    try {
                        files.document.check_ownership();
                        auto a =
                            AssetCatalog::register_audio_clip(root_, std::filesystem::u8path(wav_));
                        selection.select_asset(a.id);
                        rescan = true;
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
            }
            ui::help("Register a supported existing WAV inside this project.");
            if (prefab_controls && ImGui::CollapsingHeader("Prefabs"))
                prefab_controls();
            ui::help("Create a prefab from the selected entity, instantiate, duplicate or edit a "
                     "selected prefab asset.");
            if (asset_controls)
                asset_controls();
            ImGui::EndDisabled();
            if (locked)
                ImGui::TextWrapped(
                    "Stop Play and finish the current operation to create or register assets.");
            ImGui::EndPopup();
        }
        if (rescan)
            try {
                refresh(files);
            } catch (const std::exception& e) {
                error_ = e.what();
                refreshed_ = SDL_GetTicks();
            }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##asset-search", "Search project assets...", filter_,
                                 sizeof(filter_));
        ui::help("Search source path, generated clip name and asset type.");
        if (ImGui::BeginCombo("Type", type_.empty() ? "All assets" : type_.c_str())) {
            if (ImGui::Selectable("All assets", type_.empty()))
                type_.clear();
            std::set<std::string> types;
            if (catalog_)
                for (const auto& [id, a] : catalog_->records()) {
                    (void)id;
                    types.insert(a.type);
                }
            for (const auto& t : types)
                if (ImGui::Selectable(t.c_str(), t == type_))
                    type_ = t;
            ImGui::EndCombo();
        }
        ui::help("Show only one of the asset types currently registered in this project.");
        if (ImGui::BeginCombo("Folder", folder_[0] ? folder_ : "All folders")) {
            if (ImGui::Selectable("All folders", !folder_[0]))
                folder_[0] = 0;
            std::set<std::string> folders;
            if (catalog_)
                for (const auto& [id, asset] : catalog_->records()) {
                    (void)id;
                    for (auto p = asset.source.parent_path(); !p.empty(); p = p.parent_path())
                        folders.insert(path_text(p) + "/");
                }
            for (const auto& folder : folders)
                if (ImGui::Selectable(folder.c_str(), folder == folder_))
                    SDL_strlcpy(folder_, folder.c_str(), sizeof(folder_));
            ImGui::EndCombo();
        }
        ui::help("Browse folders containing registered assets, including descendants. All folders "
                 "searches the whole project.");
        if (ui::editor_context && ui::editor_context->reveal_content) {
            filter_[0] = folder_[0] = 0;
            type_.clear();
            ui::editor_context->reveal_content = false;
            reveal_ = true;
        }
        unsigned count = 0;
        if (ImGui::BeginTable(
                "Assets", 2,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                {0,
                 std::max(60.f, ImGui::GetContentRegionAvail().y - (error_.empty() ? 0 : 50.f))})) {
            ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
                                    110 * ui::interface_scale);
            ImGui::TableHeadersRow();
            if (catalog_)
                for (const auto& [id, a] : catalog_->records()) {
                    auto label = asset_display(a);
                    if ((!type_.empty() && a.type != type_) ||
                        search_key(label + " " + a.type).find(search_key(filter_)) ==
                            std::string::npos ||
                        !search_key(path_text(a.source)).starts_with(search_key(folder_)))
                        continue;
                    ++count;
                    ui::IdScope scope(id.str().c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool selected =
                        selection.kind() == ui::SelectionKind::Asset && selection.asset() == id;
                    if (ImGui::Selectable(
                            label.c_str(), selected,
                            ImGuiSelectableFlags(ImGuiSelectableFlags_SelectOnRelease) |
                                ImGuiSelectableFlags_AllowDoubleClick |
                                ImGuiSelectableFlags_SpanAllColumns)) {
                        selection.select_asset(id);
                        if (a.type == "scene" && ImGui::IsMouseDoubleClicked(0))
                            files.request({EditorFiles::Command::OpenScene, root_ / a.source, {}});
                    }
                    ui::help("Select to inspect this asset. Drag to a compatible asset field; "
                             "double-click scenes to open. Right-click for asset actions.");
                    if (reveal_ && selected) {
                        ImGui::SetScrollHereY();
                        reveal_ = false;
                    }
                    if (ImGui::BeginDragDropSource()) {
                        auto text = id.str();
                        ImGui::SetDragDropPayload("FORGE_ASSET", text.c_str(), text.size() + 1);
                        ImGui::Text("%s (%s)", label.c_str(), a.type.c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginPopupContextItem("Asset actions")) {
                        if (ImGui::MenuItem("Inspect"))
                            selection.select_asset(id);
                        if (a.type == "scene" && ImGui::MenuItem("Open scene"))
                            files.request({EditorFiles::Command::OpenScene, root_ / a.source, {}});
                        if (ImGui::MenuItem("Reveal source folder"))
                            SDL_OpenURL(
                                ui::local_file_url((root_ / a.source).parent_path()).c_str());
                        if (a.type == "prefab" && prefab_controls) {
                            selection.select_asset(id);
                            prefab_controls();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(a.type.c_str());
                }
            if (!count) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("No matching assets. Clear filters or use Create / Register.");
            }
            ImGui::EndTable();
        }
        if (!error_.empty()) {
            ui::field_error(error_);
            if (ui::editor_context)
                ui::editor_context->problems.report(
                    {"content", "Error", error_, {}, path_text(root_), {}, {}});
        }
        ImGui::End();
    }

  private:
    std::filesystem::path root_;
    std::optional<AssetCatalog> catalog_;
    ui::EditorSelection fallback_;
    std::string error_, type_;
    char filter_[256]{}, folder_[256]{}, wav_[1024] = "Assets/sound.wav";
    bool reveal_ = false;
    Uint64 refreshed_ = 0;
};
} // namespace forge
