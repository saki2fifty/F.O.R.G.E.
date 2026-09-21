#pragma once
#include "content_view.hpp"
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
inline std::vector<std::filesystem::path>
scene_files(const std::filesystem::path& root, std::stop_token stop = {},
            const std::set<std::filesystem::path, ProjectLocatorLess>& excluded = {}) {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    if (error)
        throw std::runtime_error("Cannot read project folder: " + error.message());
    const SourceScanOptions bounds;
    const auto entry_limit = bounds.max_files + bounds.max_directories;
    std::size_t visited = 0;
    std::uintmax_t scanned_bytes = 0;
    for (; it != end; it.increment(error)) {
        if (stop.stop_requested())
            throw std::runtime_error("Content scan cancelled");
        if (++visited > entry_limit)
            throw std::runtime_error("Scene scan exceeds " + std::to_string(entry_limit) +
                                     " entries; use File > Open scene");
        if (error)
            throw std::runtime_error("Scene scan failed: " + error.message());
        const auto& entry = *it;
        const auto name = entry.path().filename();
        if (entry.is_symlink()) {
            it.disable_recursion_pending();
            continue;
        }
        if (entry.is_directory()) {
            if (name == ".forge" || name == ".git" || std::size_t(it.depth()) >= bounds.max_depth)
                it.disable_recursion_pending();
            continue;
        }
        const auto relative = entry.path().lexically_relative(root);
        const auto filename = search_key(path_text(name));
        if (entry.is_regular_file() && search_key(path_text(entry.path().extension())) == ".json" &&
            filename != "forge.project.json" && filename != "forge.assets.json" &&
            !filename.ends_with(".forge-import.json") && !excluded.contains(relative)) {
            // Recognize legacy/custom .json scenes by structure, not just the extension.
            const auto bytes = entry.file_size();
            if (bytes > 8 * 1024 * 1024)
                continue;
            scanned_bytes += bytes;
            if (scanned_bytes > 64 * 1024 * 1024)
                throw std::runtime_error("Scene scan exceeds 64 MiB; use File > Open scene");
            try {
                const auto candidate = read_json(entry.path());
                if (!candidate.is_object() || !candidate.contains("version") ||
                    !candidate.contains("entities") || !candidate.at("entities").is_array())
                    continue;
            } catch (const std::exception&) {
                continue;
            }
            result.push_back(entry.path().lexically_relative(root));
            if (result.size() > 4096)
                throw std::runtime_error(
                    "Project has more than 4096 scene files; use File > Open scene");
        }
    }
    if (error)
        throw std::runtime_error("Scene scan failed: " + error.message());
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return search_key(path_text(a)) < search_key(path_text(b));
    });
    return result;
}
struct ContentCatalogScan {
    AssetCatalog catalog;
    std::shared_ptr<const std::vector<AssetRecord>> scenes;
    std::string diagnostic;
};
inline ContentCatalogScan
scan_content_catalog(const std::filesystem::path& root,
                     std::shared_ptr<const std::vector<AssetRecord>> previous = {},
                     std::stop_token stop = {}) {
    ContentCatalogScan result{AssetCatalog::open_project(root), std::move(previous), {}};
    try {
        std::set<std::filesystem::path, ProjectLocatorLess> excluded;
        for (const auto& [id, record] : result.catalog.records()) {
            (void)id;
            if (record.type != "scene")
                excluded.insert(record.source);
        }
        AssetCatalog discovered(root);
        auto scenes = std::make_shared<std::vector<AssetRecord>>();
        for (const auto& path : scene_files(root, stop, excluded)) {
            if (stop.stop_requested())
                throw std::runtime_error("Content scan cancelled");
            const auto doc = read_json(root / path);
            if (!doc.contains("asset_id"))
                continue;
            auto record = discovered.add_scene(path);
            const auto known = result.catalog.records().find(record.id);
            if (known != result.catalog.records().end() &&
                (known->second.type != record.type ||
                 !ProjectPaths(root).same_locator(known->second.source, record.source)))
                throw std::runtime_error(
                    "Discovered scene identity conflicts with registered asset: " +
                    path_text(path));
            scenes->push_back(std::move(record));
        }
        result.scenes = std::move(scenes);
    } catch (const std::exception& e) {
        if (stop.stop_requested())
            throw;
        result.diagnostic = std::string("Scene discovery: ") + e.what() +
                            ". Registered assets remain available; retaining the last complete "
                            "discovered-scene list.";
    }
    std::set<std::filesystem::path, ProjectLocatorLess> indexed_sources;
    for (const auto& [id, record] : result.catalog.records()) {
        (void)id;
        indexed_sources.insert(record.source);
    }
    if (result.scenes)
        for (const auto& record : *result.scenes)
            if (!result.catalog.records().contains(record.id) &&
                !indexed_sources.contains(record.source))
                result.catalog.add(record);
    return result;
}
class ContentBrowser {
  public:
    Json settings() const { return view_.settings(); }
    void load_settings(const Json& value) { view_.load_settings(value); }
    bool take_settings_changed() { return view_.take_settings_changed(); }
    const ui::AssetEditors* editors = nullptr;
    std::function<void(const AssetRecord&, bool)> file_actions;
    std::function<void()> rescan_sources, import_status;
    std::function<std::map<AssetId, ContentState>()> import_activity;
    std::function<void(const std::vector<AssetId>&)> reimport;
    std::function<bool(const std::filesystem::path&, const std::string&, bool)> open_source;
    void source_snapshot(std::shared_ptr<const SourceSnapshot> snapshot) {
        if (snapshot && snapshot->complete && snapshot != sources_) {
            sources_ = std::move(snapshot);
            index_dirty_ = true;
        }
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
    ~ContentBrowser() {
        stop_.request_stop();
        index_stop_.request_stop();
    }
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
        scan_ = std::async(std::launch::async,
                           [root = root_, previous = discovered_scenes_, stop = stop_.get_token()] {
                               return scan_content_catalog(root, previous, stop);
                           });
    }
    void poll(EditorFiles& files) {
        select_project(files);
        poll_index();
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
                merge_prefabs(candidate.catalog, files);
                catalog_ = std::make_shared<AssetCatalog>(std::move(candidate.catalog));
                discovered_scenes_ = std::move(candidate.scenes);
                index_dirty_ = true;
                error_ = std::move(candidate.diagnostic);
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
                catalog_ = std::make_shared<AssetCatalog>(root_);
            if (!record(id)) {
                auto next = std::make_shared<AssetCatalog>(*catalog_);
                next->add(known->second);
                catalog_ = std::move(next);
                index_dirty_ = true;
            }
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
        if (ImGui::TreeNode("Dependencies and references")) {
            const auto& graph = catalog_->dependency_graph();
            const auto link = [&](AssetId id) {
                ui::IdScope item(id.str().c_str());
                const auto* target = record(id);
                ImGui::TextWrapped("%s",
                                   target ? asset_display(*target).c_str() : id.str().c_str());
                if (ui::button("Select asset",
                               "Inspect this catalog dependency or direct referring asset.")) {
                    selection.select_asset(id);
                    if (ui::editor_context)
                        ui::editor_context->reveal_content = true;
                }
            };
            ImGui::TextUnformatted("Uses assets");
            ui::help("Typed catalog edges. These are build/runtime dependency records, not every "
                     "reference in unopened scene or opaque plugin data.");
            ImGui::PushID("forward");
            for (const auto& edge : graph.dependencies(asset->id)) {
                ui::IdScope role(edge.role.c_str());
                const Json encoded = edge;
                ImGui::TextWrapped("%s / %s", encoded.at("kind").get<std::string>().c_str(),
                                   edge.role.c_str());
                link(edge.target);
            }
            ImGui::PopID();
            ImGui::TextUnformatted("Uses source files");
            ui::help("Captured source dependencies such as external glTF images or shader "
                     "includes; these paths are not allocated AssetIds.");
            for (const auto& source : graph.source_dependencies(asset->id))
                ImGui::TextWrapped("%s / %s", path_text(source.source).c_str(),
                                   source.role.c_str());
            ImGui::TextUnformatted("Referenced by assets");
            ui::help("Direct catalog referrers. Source deletion performs a separate reviewed scan "
                     "of known authored references.");
            ImGui::PushID("reverse");
            for (const auto id : graph.referrers(asset->id))
                link(id);
            ImGui::PopID();
            ImGui::TreePop();
        }
        ui::help("Inspect the existing catalog's forward/reverse dependency graph without changing "
                 "asset contents.");
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
        view_.reimport =
            reimport ? std::function<void(const std::vector<AssetId>&)>([&](const auto& ids) {
                try {
                    reimport(ids);
                    error_.clear();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            })
                     : std::function<void(const std::vector<AssetId>&)>{};
        view_.open = [&](const ContentEntry& entry) {
            if (entry.asset) {
                if (const auto* a = record(entry.asset); a && editors)
                    editors->open(*a);
            } else if (open_source)
                open_source(entry.source, entry.type, true);
        };
        view_.context_menu = [&](const ContentEntry& entry) {
            const auto* a = entry.asset ? record(entry.asset) : nullptr;
            if (a && editors) {
                if (const auto* editor = editors->find(a->type);
                    editor && ImGui::MenuItem(editor->label.c_str(), nullptr, false, !locked))
                    editors->open(*a);
            } else if (!entry.asset && open_source &&
                       open_source(entry.source, entry.type, false) &&
                       ImGui::MenuItem("Open import / source", nullptr, false, !locked)) {
                open_source(entry.source, entry.type, true);
            }
            if (ImGui::MenuItem("Reveal source folder"))
                SDL_OpenURL(ui::local_file_url((root_ / entry.source).parent_path()).c_str());
            if (a && file_actions)
                file_actions(*a, locked);
            if (a && a->type == "prefab" && prefab_controls) {
                selection.select_asset(a->id);
                prefab_controls();
            }
        };
        if (ui::editor_context && ui::editor_context->reveal_content && index_) {
            view_.reveal(selection);
            ui::editor_context->reveal_content = false;
        }
        view_.draw(selection, locked);
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
    std::shared_ptr<AssetCatalog> catalog_;
    std::shared_ptr<const ContentIndex> index_;
    std::future<std::shared_ptr<const ContentIndex>> index_job_;
    std::stop_source index_stop_;
    bool index_dirty_ = true;
    Uint64 indexed_at_ = 0;
    ContentView view_;
    std::shared_ptr<const SourceSnapshot> sources_;
    ui::EditorSelection fallback_;
    AssetId inspected_;
    std::string error_;
    char wav_[1024] = "Assets/sound.wav";
    Uint64 refreshed_ = 0;
    bool refresh_again_ = false;
    std::filesystem::path scan_project_;
    std::stop_source stop_;
    std::future<ContentCatalogScan> scan_;
    std::shared_ptr<const std::vector<AssetRecord>> discovered_scenes_;
    void select_project(EditorFiles& files) {
        if (root_ == files.document.project())
            return;
        stop_.request_stop();
        root_ = files.document.project();
        catalog_.reset();
        discovered_scenes_.reset();
        sources_.reset();
        index_stop_.request_stop();
        index_.reset();
        const auto preferences = view_.settings();
        view_ = ContentView{};
        view_.load_settings(preferences);
        index_dirty_ = true;
        error_.clear();
        refreshed_ = 0;
        refresh_again_ = true;
    }
    void poll_index() {
        if (index_job_.valid()) {
            if (index_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            try {
                auto result = index_job_.get();
                if (!index_stop_.stop_requested()) {
                    index_ = std::move(result);
                    view_.update(index_);
                }
            } catch (const std::exception& e) {
                if (!index_stop_.stop_requested())
                    error_ = e.what();
            }
        }
        if (catalog_ && (index_dirty_ || SDL_GetTicks() - indexed_at_ > 2000)) {
            indexed_at_ = SDL_GetTicks();
            auto activity = import_activity ? import_activity() : std::map<AssetId, ContentState>{};
            index_dirty_ = false;
            index_stop_ = std::stop_source{};
            index_job_ = std::async(
                std::launch::async,
                [catalog = std::shared_ptr<const AssetCatalog>(catalog_), sources = sources_,
                 stop = index_stop_.get_token(), activity = std::move(activity)] {
                    return std::make_shared<const ContentIndex>(
                        ContentIndex::build(*catalog, sources.get(), stop, activity));
                });
        }
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
