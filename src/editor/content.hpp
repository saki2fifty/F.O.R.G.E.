#pragma once
#include "document_workspace.hpp"
#include "files.hpp"
#include "help.hpp"
#include "property_drawer.hpp"
#include "search.hpp"
#include <algorithm>
#include <cctype>
#include <forge/asset_discovery.hpp>
#include <future>
#include <set>
#include <stop_token>
namespace forge {
inline std::vector<std::filesystem::path> scene_files(const std::filesystem::path& root,
                                                      std::stop_token stop = {}) {
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
        if (stop.stop_requested())
            throw std::runtime_error("Content scan cancelled");
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
    const ui::AssetEditors* editors = nullptr;
    std::function<void()> rescan_sources, import_status;
    std::function<bool(const std::filesystem::path&, const std::string&, bool)> open_source;
    void source_snapshot(std::shared_ptr<const SourceSnapshot> snapshot) {
        if (snapshot && snapshot->complete)
            sources_ = std::move(snapshot);
    }
    void inspect_source(const std::string& locator) {
        const auto path = std::filesystem::u8path(locator);
        if (!sources_) {
            ui::field_error("Source discovery is not ready.");
            return;
        }
        const auto found = sources_->files.find(path);
        if (found == sources_->files.end()) {
            ui::field_error("This source is no longer present in the last complete scan.");
            return;
        }
        const auto& source = found->second;
        ImGui::TextWrapped("Source: %s", locator.c_str());
        ui::help("A project file, not yet a registered logical asset. Import prepares a typed "
                 "asset without changing scene content.");
        ImGui::Text("Type: %s", source.source_kind.c_str());
        ui::help("Recognized file kind; import still validates its actual contents.");
        ImGui::Text("Size: %.1f KiB", double(source.bytes) / 1024);
        ui::help("Source file size from the last complete background scan.");
        if (open_source && open_source(path, source.source_kind, false)) {
            if (ui::button("Open import / source", "Review this source in its registered central "
                                                   "asset document. No scene edit."))
                open_source(path, source.source_kind, true);
        } else {
            ImGui::TextWrapped("Use Create / Register for this source type's available tools.");
        }
        if (ui::button("Reveal source folder", "Open this project's source directory."))
            SDL_OpenURL(ui::local_file_url((root_ / path).parent_path()).c_str());
    }
    const AssetRecord* record(AssetId id) const {
        if (!catalog_)
            return nullptr;
        const auto it = catalog_->records().find(id);
        return it == catalog_->records().end() ? nullptr : &it->second;
    }
    ~ContentBrowser() { stop_.request_stop(); }
    bool refreshing() const { return scan_.valid() || refresh_again_; }
    void refresh(EditorFiles& files) {
        select_project(files);
        refreshed_ = SDL_GetTicks();
        if (scan_.valid()) {
            refresh_again_ = true;
            return;
        }
        refresh_again_ = false;
        stop_ = std::stop_source{};
        scan_project_ = root_;
        scan_ = std::async(std::launch::async, [root = root_, stop = stop_.get_token()] {
            auto candidate = AssetCatalog::open_project(root);
            // Scenes carry their own AssetIds. Browsing creates no new identity
            // and writes no replacement asset database.
            for (const auto& path : scene_files(root, stop)) {
                if (stop.stop_requested())
                    throw std::runtime_error("Content scan cancelled");
                const auto doc = read_json(root / path);
                if (!doc.contains("asset_id"))
                    continue;
                const auto id = doc.at("asset_id").get<AssetId>();
                if (!candidate.records().contains(id))
                    candidate.add_scene(path);
            }
            return candidate;
        });
    }
    void poll(EditorFiles& files) {
        select_project(files);
        if (!scan_.valid()) {
            if (std::exchange(refresh_again_, false))
                refresh(files);
            return;
        }
        if (scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            auto candidate = scan_.get();
            if (!stop_.stop_requested() && scan_project_ == root_) {
                merge_prefabs(candidate, files);
                catalog_ = std::move(candidate);
                error_.clear();
                refreshed_ = SDL_GetTicks();
            }
        } catch (const std::exception& e) {
            if (!stop_.stop_requested() && scan_project_ == root_) {
                error_ = e.what();
                refreshed_ = SDL_GetTicks();
            }
        }
        if (std::exchange(refresh_again_, false))
            refresh(files);
    }
    const AssetRecord* resolve_record(EditorFiles& files, AssetId id) {
        poll(files);
        const bool changed = inspected_ != id;
        // Prefab authoring already has an owned validated record; expose that
        // immediately while disk discovery runs in the background.
        if (const auto known = files.document.prefabs().records().find(id);
            known != files.document.prefabs().records().end()) {
            if (!catalog_)
                catalog_.emplace(root_);
            if (!record(id))
                catalog_->add(known->second);
        }
        inspected_ = id;
        if (root_ != files.document.project() || (!catalog_ && refreshed_ == 0) ||
            (changed && !record(id)) || SDL_GetTicks() - refreshed_ > 5000) {
            try {
                refresh(files);
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
                refreshed_ = SDL_GetTicks();
                if (root_ != files.document.project())
                    catalog_.reset();
            }
        }
        return record(id);
    }
    void inspect(EditorFiles& files, ui::EditorSelection& selection,
                 const std::function<void(AssetId)>& open_prefab = {}) {
        const auto* asset = resolve_record(files, selection.asset());
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
        if (editors) {
            if (const auto* editor = editors->find(asset->type);
                editor &&
                ui::button(editor->label.c_str(),
                           "Open this asset through its registered editor and save guards."))
                editors->open(*asset);
        } else if (asset->type == "prefab" && open_prefab &&
                   ui::button("Edit prefab source", "Open the independent prefab draft."))
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
        poll(files);
        if (ui::editor_context && ui::editor_context->reveal_content)
            ImGui::SetNextWindowFocus();
        if (!ImGui::Begin("Content", open)) {
            ImGui::End();
            return;
        }
        auto& selection = ui::editor_context ? ui::editor_context->selection : fallback_;
        if (ui::editor_context)
            ui::editor_context->task.focus(ui::DocumentTask::Scene);
        bool rescan = !refreshing() && (refreshed_ == 0 || SDL_GetTicks() - refreshed_ > 5000);
        if (ui::button("Create / Register", "Create or register supported project assets. These "
                                            "operations are separate from scene Undo."))
            ImGui::OpenPopup("Asset operations");
        ImGui::SameLine();
        if (ui::button("Refresh",
                       "Refresh registered assets and discover saved scene documents.")) {
            rescan = true;
            error_.clear();
            if (rescan_sources)
                rescan_sources();
        }
        if (import_status) {
            ui::next_text_button("Source updates");
            import_status();
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
        if (refreshing()) {
            ImGui::SameLine();
            ImGui::TextDisabled("Refreshing...");
            ui::help(
                "Project asset discovery runs in the background. Existing results stay available; "
                "a failed scan keeps the last complete list.");
        }
        const bool wide_filters = ImGui::GetContentRegionAvail().x >= 600 * ui::interface_scale;
        if (wide_filters)
            ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##asset-search", "Search project assets...", filter_,
                                 sizeof(filter_));
        ui::help("Search source path, generated clip name and asset type.");
        if (wide_filters)
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .35f);
        else
            ImGui::SetNextItemWidth(
                std::max(20.f, ImGui::GetContentRegionAvail().x - 60 * ui::interface_scale));
        if (ImGui::BeginCombo("Type", type_.empty() ? "All assets" : type_.c_str())) {
            if (ImGui::Selectable("All assets", type_.empty()))
                type_.clear();
            if (ImGui::Selectable("Source files", type_ == "Source files"))
                type_ = "Source files";
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
        if (wide_filters)
            ImGui::SameLine();
        ImGui::SetNextItemWidth(
            std::max(20.f, ImGui::GetContentRegionAvail().x - 65 * ui::interface_scale));
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
            if (sources_)
                for (const auto& [path, source] : sources_->files) {
                    (void)source;
                    for (auto p = path.parent_path(); !p.empty(); p = p.parent_path())
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
                        if (!locked && editors && ImGui::IsMouseDoubleClicked(0))
                            editors->open(a);
                    }
                    ui::help("Select to inspect this asset. Drag to a compatible asset field; "
                             "double-click supported editable assets to open. Right-click for "
                             "asset actions.");
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
                        if (editors)
                            if (const auto* editor = editors->find(a.type);
                                editor &&
                                ImGui::MenuItem(editor->label.c_str(), nullptr, false, !locked))
                                editors->open(a);
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
            if (sources_ && (type_.empty() || type_ == "Source files")) {
                std::set<std::filesystem::path, ProjectLocatorLess> registered;
                if (catalog_)
                    for (const auto& [id, record] : catalog_->records()) {
                        (void)id;
                        registered.insert(record.source);
                    }
                for (const auto& [path, source] : sources_->files) {
                    const auto label = path_text(path);
                    if (registered.contains(path) || source.source_kind == "unrecognized" ||
                        !source.alias_of.empty() ||
                        search_key(label + " " + source.source_kind).find(search_key(filter_)) ==
                            std::string::npos ||
                        !search_key(label).starts_with(search_key(folder_)))
                        continue;
                    ++count;
                    ui::IdScope scope(label.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool selected = selection.kind() == ui::SelectionKind::DocumentItem &&
                                          selection.document() == "content.source" &&
                                          selection.member() == label;
                    if (ImGui::Selectable(label.c_str(), selected,
                                          ImGuiSelectableFlags_AllowDoubleClick |
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                        selection.select_document_item("content.source", label);
                        if (ui::editor_context)
                            ui::editor_context->task.focus_document("content.source",
                                                                    "Source file");
                        if (!locked && ImGui::IsMouseDoubleClicked(0) && open_source)
                            open_source(path, source.source_kind, true);
                    }
                    ui::help("Unimported source. Select for source information; double-click "
                             "supported types to review import or material source settings. No "
                             "persistent asset identity is allocated by browsing.");
                    ImGui::TableNextColumn();
                    ImGui::Text("Source / %s", source.source_kind.c_str());
                    ui::help("This file is not registered as a logical asset yet.");
                }
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
    std::shared_ptr<const SourceSnapshot> sources_;
    ui::EditorSelection fallback_;
    AssetId inspected_;
    std::string error_, type_;
    char filter_[256]{}, folder_[256]{}, wav_[1024] = "Assets/sound.wav";
    bool reveal_ = false;
    Uint64 refreshed_ = 0;
    bool refresh_again_ = false;
    std::filesystem::path scan_project_;
    std::stop_source stop_;
    std::future<AssetCatalog> scan_;
    void select_project(EditorFiles& files) {
        if (root_ == files.document.project())
            return;
        stop_.request_stop();
        root_ = files.document.project();
        catalog_.reset();
        sources_.reset();
        error_.clear();
        refreshed_ = 0;
        refresh_again_ = true;
    }
    static void merge_prefabs(AssetCatalog& candidate, EditorFiles& files) {
        for (const auto& [id, prefab] : files.document.prefabs().records()) {
            if (!candidate.records().contains(id))
                candidate.add(prefab);
            else if (candidate.records().at(id).type != prefab.type ||
                     candidate.records().at(id).source != prefab.source)
                throw std::runtime_error("Conflicting prefab asset identity in Content");
        }
    }
};
} // namespace forge
