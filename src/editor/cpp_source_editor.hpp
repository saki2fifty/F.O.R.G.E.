#pragma once
#include "cpp_source_files.hpp"
#include "cpp_source_search.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include "widgets.hpp"
#include <SDL3/SDL.h>
#include <TextEditor.h>
#include <cstdint>
#include <imgui_internal.h>
#include <memory>

namespace forge {
struct CodeAutomation;
}
namespace forge::ui {
class CppSourceEditor {
    friend struct ::forge::CodeAutomation;
    struct Tab {
        std::string locator, disk, saved;
        std::string session = AssetId::generate().str(); // Transient open-tab routing only.
        TextEditor editor;
        bool dirty = false, selected = false, external = false;
        std::filesystem::file_time_type stamp{};
        explicit Tab(std::string path, std::string text) : locator(std::move(path)), disk(text) {
            auto language = TextEditor::LanguageDefinition::CPlusPlus();
            // No upstream identifier popups: all FORGE help obeys the global Tooltips switch.
            language.mIdentifiers.clear();
            language.mPreprocIdentifiers.clear();
            editor.SetLanguageDefinition(language);
            editor.SetShowWhitespaces(false);
            editor.SetTabSize(4);
            editor.SetText(text);
            saved = editor.GetText();
        }
        void changed() { dirty = editor.GetText() != saved; }
    };
    std::filesystem::path root_;
    std::vector<std::unique_ptr<Tab>> tabs_;
    Tab* active_ = nullptr;
    bool open_ = false, focus_ = false, close_all_ = false, close_popup_ = false;
    std::string close_target_, error_, search_;
    char locator_[256] = "Native/gameplay.cpp", query_[256] = "";
    std::size_t search_from_ = 0;
    Uint64 checked_disk_ = 0;
    CppSourceSearch project_search_;
    std::optional<CppSearchResult> project_matches_;
    std::filesystem::path search_project_;
    std::string search_operation_;
    CppSearchOptions search_options_;
    char project_query_[256] = "";
    bool project_case_ = true, project_word_ = false, show_project_search_ = false;
    Tab* find(const std::string& locator) {
        ProjectPaths paths(root_);
        for (auto& tab : tabs_)
            if (paths.same_locator(tab->locator, locator))
                return tab.get();
        return nullptr;
    }
    void erase(std::string locator) {
        const bool was_active = active_ && active_->locator == locator;
        std::erase_if(tabs_, [&](const auto& tab) { return tab->locator == locator; });
        if (was_active)
            active_ = tabs_.empty() ? nullptr : tabs_.back().get();
        open_ = !tabs_.empty();
    }
    template <class F> void attempt(F&& f) {
        try {
            f();
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }

  public:
    bool close_cancelled = false, build_on_save = false, build_pending = false;
    bool build_enabled = false;
    std::uint64_t source_revision = 0;
    std::function<void()> request_build, request_new;
    // Main supplies the effective shared Save binding, including duplicate-dispatch guards.
    std::function<bool()> save_shortcut = [] {
        return ImGui::GetIO().KeyMods == ImGuiMod_Ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false);
    };
    std::function<void(const std::string&)> request_reveal;
    bool is_open() const { return open_; }
    bool owns_keyboard() const {
        if (!open_ || !ImGui::GetCurrentContext())
            return false;
        const auto* owner = ImGui::FindWindowByName("###C++ Sources");
        for (const auto* window = ImGui::GetCurrentContext()->NavWindow; window;
             window = window->ParentWindow)
            if (window == owner)
                return true;
        return false;
    }
    std::string active_source() const { return active_ ? active_->locator : std::string{}; }
    bool dirty_source(const std::string& locator) const {
        for (const auto& tab : tabs_)
            if (tab->locator == locator)
                return tab->dirty;
        return false;
    }
    std::string active_text() const { return active_ ? active_->editor.GetText() : std::string{}; }
    void poll_project_search() {
        if (auto result = project_search_.poll()) {
            if (search_project_ == root_)
                project_matches_ = std::move(*result);
        }
    }
    void search_project(SceneDocument& project, CppSearchOptions options, bool reveal = true) {
        project.check_ownership();
        validate_cpp_search(options);
        if (root_ != project.project())
            project_changed(project.project());
        poll_project_search();
        if (project_search_.busy())
            throw std::runtime_error("Wait for the current project search or cancel it first.");
        CppSearchDrafts drafts;
        std::size_t bytes = 0;
        for (const auto& tab : tabs_) {
            auto text = tab->editor.GetText();
            if (text.size() > cpp_search_byte_limit - bytes)
                throw std::runtime_error("Open source drafts exceed the 32 MiB search budget.");
            bytes += text.size();
            drafts.emplace(std::filesystem::u8path(tab->locator), std::move(text));
        }
        project_search_.start(root_, options, std::move(drafts));
        search_project_ = root_;
        search_operation_ = AssetId::generate().str();
        search_options_ = options;
        project_matches_.reset();
        std::copy(options.query.begin(), options.query.end(), project_query_);
        project_query_[options.query.size()] = '\0';
        project_case_ = options.match_case;
        project_word_ = options.whole_word;
        show_project_search_ = true;
        if (reveal)
            open_ = focus_ = true;
    }
    void open_search_result(SceneDocument& project, std::size_t index) {
        if (search_project_ != project.project() || !project_matches_ ||
            index >= project_matches_->matches.size())
            throw std::runtime_error("Run Find in Project again before opening this result.");
        const auto match = project_matches_->matches[index];
        open(project, match.path, match.line, match.column, match.state);
        const auto row = match.line - 1, column = match.column - 1;
        const auto [end_row, end_column] =
            cpp_source_coordinate(search_options_.query, match.length, 0, row, column);
        active_->editor.SetSelection({row, column}, {end_row, end_column});
        active_->editor.SetCursorPosition({end_row, end_column});
    }
    void replace_text(const std::string& text) {
        validate_cpp_text(text);
        if (text.size() >= max_cpp_source_bytes)
            throw std::runtime_error(
                "Leave room for the source editor's final newline within 1 MiB");
        if (!active_ || active_->editor.IsReadOnly())
            throw std::runtime_error("Open an editable C++ source tab first");
        if (!ImGui::GetCurrentContext())
            throw std::runtime_error("C++ editing requires the owning editor UI context");
        if (active_->editor.GetText() == text)
            return;
        // GetText includes the widget's implicit final newline. Do not paste
        // that newline as an additional empty line. A missing final newline is
        // normalized by the same widget used for interactive edits.
        std::string contents = text;
        if (!contents.empty() && contents.back() == '\n')
            contents.pop_back();
        // Reuse the widget's ordinary single Paste/Delete Undo record. Substitute
        // only its synchronous clipboard reader; never read/write the OS clipboard.
        auto& platform = ImGui::GetPlatformIO();
        struct ClipboardText {
            const char* text;
        } source{contents.c_str()};
        struct RestoreClipboard {
            ImGuiPlatformIO& platform;
            decltype(platform.Platform_GetClipboardTextFn) get;
            void* data;
            ~RestoreClipboard() {
                platform.Platform_GetClipboardTextFn = get;
                platform.Platform_ClipboardUserData = data;
            }
        } restore{platform, platform.Platform_GetClipboardTextFn,
                  platform.Platform_ClipboardUserData};
        platform.Platform_ClipboardUserData = &source;
        platform.Platform_GetClipboardTextFn = [](ImGuiContext* context) {
            return static_cast<const ClipboardText*>(context->PlatformIO.Platform_ClipboardUserData)
                ->text;
        };
        active_->editor.SelectAll();
        if (contents.empty())
            active_->editor.Delete();
        else
            active_->editor.Paste();
        active_->changed();
    }
    bool dirty() const {
        return std::any_of(tabs_.begin(), tabs_.end(), [](const auto& tab) { return tab->dirty; });
    }
    bool can_undo() const { return active_ && active_->editor.CanUndo(); }
    bool can_redo() const { return active_ && active_->editor.CanRedo(); }
    void undo(bool redo) {
        if (!active_)
            return;
        if (redo)
            active_->editor.Redo();
        else
            active_->editor.Undo();
        active_->changed();
    }
    void project_changed(const std::filesystem::path& root) {
        project_search_.cancel();
        project_matches_.reset();
        search_project_.clear();
        search_operation_.clear();
        show_project_search_ = false;
        root_ = root;
        tabs_.clear();
        active_ = nullptr;
        open_ = focus_ = false;
        close_all_ = close_popup_ = build_pending = false;
        error_.clear();
        ++source_revision;
    }
    void open(SceneDocument& project, const std::string& locator, int line = 1, int column = 1,
              const std::string& expected_state = {}) {
        project.check_ownership();
        if (root_ != project.project())
            project_changed(project.project());
        const auto normalized =
            path_utf8(ProjectPaths::normalize(std::filesystem::u8path(locator)));
        auto* tab = find(normalized);
        if (!tab) {
            auto path = cpp_source_path(root_, normalized);
            if (tabs_.size() >= 32)
                throw std::runtime_error("Close a source tab before opening more than 32 files.");
            const auto text = read_cpp_source(path);
            if (!expected_state.empty() && cpp_source_state(text) != expected_state)
                throw CppSourceChanged();
            tabs_.push_back(std::make_unique<Tab>(normalized, text));
            tab = tabs_.back().get();
            tab->stamp = std::filesystem::last_write_time(path);
        } else if (tab->dirty) {
            if (!expected_state.empty() &&
                cpp_source_state(tab->editor.GetText()) != expected_state)
                throw CppSourceChanged();
        } else {
            const auto path = cpp_source_path(root_, normalized);
            const auto disk = read_cpp_source(path);
            if (!expected_state.empty() &&
                (disk != tab->disk || cpp_source_state(tab->editor.GetText()) != expected_state))
                throw CppSourceChanged();
            if (disk != tab->disk) {
                tab->disk = disk;
                tab->editor.SetText(disk);
                tab->saved = tab->editor.GetText();
                tab->stamp = std::filesystem::last_write_time(path);
                tab->external = false;
            }
        }
        tab->selected = true;
        active_ = tab;
        open_ = focus_ = true;
        tab->editor.SetCursorPosition(
            TextEditor::Coordinates(std::max(line - 1, 0), std::max(column - 1, 0)));
    }
    void save(SceneDocument& project, bool queue_build = true) {
        if (!active_ || !active_->dirty)
            return;
        project.check_ownership();
        if (root_ != project.project())
            throw std::runtime_error("Source belongs to a previous project.");
        const auto text = active_->editor.GetText();
        save_cpp_source(root_, active_->locator, active_->disk, text);
        active_->saved = active_->disk = text;
        active_->dirty = false;
        active_->stamp = std::filesystem::last_write_time(cpp_source_path(root_, active_->locator));
        active_->external = false;
        ++source_revision;
        if (build_on_save && queue_build)
            build_pending = true;
    }
    void save_all(SceneDocument& project, bool queue_build = true) {
        auto* previous = active_;
        try {
            for (auto& tab : tabs_)
                if (tab->dirty) {
                    active_ = tab.get();
                    save(project, queue_build);
                }
        } catch (...) {
            active_ = previous;
            throw;
        }
        active_ = previous;
    }
    void request_close() {
        close_all_ = true;
        close_popup_ = true;
    }
    void draw(SceneDocument& project, EditorUiContext& context, bool locked,
              bool save_locked = false) {
        poll_project_search();
        if (!open_)
            return;
        if (root_ != project.project()) {
            project_changed(project.project());
            return;
        }
        if (SDL_GetTicks() - checked_disk_ > 1000) {
            checked_disk_ = SDL_GetTicks();
            for (auto& tab : tabs_) {
                try {
                    std::error_code ec;
                    const auto stamp = std::filesystem::last_write_time(
                        ProjectPaths(root_).resolve(tab->locator), ec);
                    tab->external = bool(ec) || stamp != tab->stamp;
                } catch (const std::exception& e) {
                    tab->external = true;
                    error_ = e.what();
                }
            }
        }
        bool visible = true;
        if (const auto* scene = ImGui::FindWindowByName("Scene###Scene"))
            if (scene->DockId)
                ImGui::SetNextWindowDockID(scene->DockId, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({800, 550}, ImGuiCond_FirstUseEver);
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        const bool contents = ImGui::Begin("C++ Sources###C++ Sources", &visible);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            context.task.focus_document("cpp_sources", "C++ Sources");
        if (!visible)
            request_close();
        if (contents) {
            const bool save_key = !locked && !save_locked && save_shortcut && save_shortcut() &&
                                  ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            controls(project, locked || save_locked);
            ImGui::BeginDisabled(locked || save_locked);
            const bool active_dirty = active_ && active_->dirty;
            if (active_dirty)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
            if (button(active_dirty ? "Save source *" : "Save source",
                       "Save only the active source file; scene Save is separate."))
                attempt([&] { save(project); });
            if (active_dirty)
                ImGui::PopStyleColor();
            ImGui::SameLine();
            if (button("Reload from disk",
                       "Discard this source draft and source history after confirmation.")) {
                if (active_) {
                    close_target_ = active_->locator;
                    ImGui::OpenPopup("Reload C++ source?");
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Checkbox("Build on Save", &build_on_save) && !build_on_save)
                build_pending = false;
            FORGE_UI_PROBE("cpp:build-on-save");
            help("Queue a managed gameplay build after saving. Waits until Play stops and all "
                 "source drafts are saved. Disabling affects future saves; cancel an active "
                 "build from Build.");
            ImGui::BeginDisabled(!build_enabled || !request_build);
            if (button(dirty() ? "Save & Build Project Code" : "Build Project Code",
                       "Build all saved project C++ sources using the existing "
                       "isolated compiler task. Stop Play first."))
                attempt([&] { request_build(); });
            ImGui::EndDisabled();
            if (!save_key && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
                ImGui::IsKeyPressed(ImGuiKey_F) &&
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Find", query_, sizeof(query_),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                search_from_ = 0;
            help("Case-sensitive search in the active source. Find next wraps to the start.");
            ImGui::SameLine();
            if (button("Find next", "Select the next occurrence of the search text.")) {
                if (active_ && query_[0]) {
                    const auto text = active_->editor.GetText();
                    if (search_ != query_)
                        search_from_ = 0;
                    search_ = query_;
                    auto pos = text.find(search_, search_from_);
                    if (pos == std::string::npos)
                        pos = text.find(search_);
                    if (pos != std::string::npos) {
                        auto coordinate = [&](std::size_t end) {
                            const auto [row, col] = cpp_source_coordinate(text, end);
                            return TextEditor::Coordinates(row, col);
                        };
                        active_->editor.SetSelection(coordinate(pos),
                                                     coordinate(pos + search_.size()));
                        active_->editor.SetCursorPosition(coordinate(pos + search_.size()));
                        search_from_ = pos + search_.size();
                    } else
                        error_ = "No match in the active source.";
                }
            }
            draw_project_search(project, locked);
            if (ImGui::BeginTabBar("cpp_source_tabs")) {
                std::string closed;
                for (auto& tab : tabs_) {
                    bool tab_open = true;
                    ImGuiTabItemFlags flags =
                        tab->dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                    if (std::exchange(tab->selected, false))
                        flags |= ImGuiTabItemFlags_SetSelected;
                    if (ImGui::BeginTabItem(tab->locator.c_str(), &tab_open, flags)) {
                        active_ = tab.get();
                        if (tab->external)
                            ImGui::TextWrapped("File changed outside FORGE. Reload explicitly; "
                                               "your draft is retained.");
                        tab->editor.SetReadOnly(locked);
                        tab->editor.SetHandleKeyboardInputs(!save_key);
                        const auto available = ImGui::GetContentRegionAvail();
                        tab->editor.Render("C++ text",
                                           {available.x, std::max(80.0f, available.y - 35.0f)});
                        tab->editor.SetHandleKeyboardInputs(true);
                        FORGE_UI_PROBE("cpp:editor");
                        if (tab->editor.IsTextChanged())
                            tab->changed();
                        const auto cursor = tab->editor.GetCursorPosition();
                        ImGui::Text("Line %d, column %d | %s", cursor.mLine + 1, cursor.mColumn + 1,
                                    tab->dirty ? "Unsaved source" : "Saved source");
                        help("C++ highlighting, four-space tab stops, source-only Undo/Redo. "
                             "Compiler tools run separately. Source files up to 1 MiB; no "
                             "arbitrary C++ hot reload.");
                        ImGui::EndTabItem();
                    }
                    if (!tab_open) {
                        if (tab->dirty) {
                            close_target_ = tab->locator;
                            close_popup_ = true;
                        } else
                            closed = tab->locator;
                    }
                }
                ImGui::EndTabBar();
                if (!closed.empty())
                    erase(closed);
            }
            if (!save_key && !locked && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
                ImGui::IsKeyPressed(ImGuiKey_Z) &&
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                undo(true);
            if (save_key)
                attempt([&] { save(project); });
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
        }
        if (close_popup_) {
            ImGui::OpenPopup("Unsaved C++ source");
            close_popup_ = false;
        }
        if (ImGui::BeginPopupModal("Unsaved C++ source", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            auto* tab = close_all_ ? nullptr : find(close_target_);
            if (close_all_)
                for (auto& t : tabs_)
                    if (t->dirty) {
                        tab = t.get();
                        break;
                    }
            if (!tab) {
                tabs_.clear();
                active_ = nullptr;
                open_ = false;
                close_all_ = false;
                ImGui::CloseCurrentPopup();
            } else {
                active_ = tab;
                ImGui::TextWrapped("Save changes to %s? Source history belongs to this file.",
                                   tab->locator.c_str());
                ImGui::BeginDisabled(locked || save_locked);
                if (button("Save and close",
                           "Save with external-change checking, then close this source."))
                    attempt([&] {
                        save(project);
                        const auto name = tab->locator;
                        erase(name);
                        if (close_all_)
                            close_popup_ = true;
                        ImGui::CloseCurrentPopup();
                    });
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (button("Discard source",
                           "Discard this draft without changing the file on disk.")) {
                    const auto name = tab->locator;
                    erase(name);
                    if (close_all_)
                        close_popup_ = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (button("Cancel", "Keep the source open and cancel switching or closing.")) {
                    close_all_ = false;
                    close_cancelled = true;
                    ImGui::CloseCurrentPopup();
                }
                if (!error_.empty())
                    ImGui::TextWrapped("%s", error_.c_str());
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Reload C++ source?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Discard the current source draft and reload the disk version?");
            if (button("Reload",
                       "Read the current disk version; clears this source's Undo history."))
                attempt([&] {
                    auto* tab = find(close_target_);
                    if (!tab)
                        throw std::runtime_error("Source tab closed.");
                    tab->disk = read_cpp_source(cpp_source_path(root_, tab->locator));
                    tab->editor.SetText(tab->disk);
                    tab->saved = tab->editor.GetText();
                    tab->dirty = false;
                    tab->external = false;
                    tab->stamp =
                        std::filesystem::last_write_time(cpp_source_path(root_, tab->locator));
                    ImGui::CloseCurrentPopup();
                });
            ImGui::SameLine();
            if (button("Cancel", "Keep your source draft."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            ImGui::EndPopup();
        }
        ImGui::End();
    }
    void draw_project_search(SceneDocument& project, bool locked) {
        const bool shortcut = ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
                              ImGui::IsKeyPressed(ImGuiKey_F, false) &&
                              ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (shortcut || show_project_search_)
            ImGui::SetNextItemOpen(true);
        show_project_search_ = ImGui::CollapsingHeader("Find in Project");
        FORGE_UI_PROBE("cpp:project-search");
        if (!show_project_search_)
            return;
        if (shortcut)
            ImGui::SetKeyboardFocusHere();
        const bool enter =
            ImGui::InputTextWithHint("Text##project-search", "Literal text...", project_query_,
                                     sizeof(project_query_), ImGuiInputTextFlags_EnterReturnsTrue);
        FORGE_UI_PROBE("cpp:project-query");
        ImGui::Checkbox("Match case", &project_case_);
        FORGE_UI_PROBE("cpp:project-case");
        ImGui::SameLine();
        ImGui::Checkbox("Whole word", &project_word_);
        FORGE_UI_PROBE("cpp:project-word");
        help("Search supported Native source/header files and snapshots of open tabs. "
             "Case folding is ASCII; non-ASCII letters retain exact bytes and word boundaries. "
             "No regular expressions or file changes. Rerun after editing.");
        ImGui::BeginDisabled(project_search_.busy() || !project_query_[0]);
        if (button("Search project",
                   "Search in the background; results open the matching source.") ||
            (enter && !project_search_.busy() && project_query_[0]))
            attempt(
                [&] { search_project(project, {project_query_, project_case_, project_word_}); });
        FORGE_UI_PROBE("cpp:project-run");
        ImGui::EndDisabled();
        if (project_search_.busy()) {
            ImGui::SameLine();
            if (button("Cancel search", "Stop this search without changing sources."))
                project_search_.cancel();
            FORGE_UI_PROBE("cpp:project-cancel");
            ImGui::SameLine();
            ImGui::TextUnformatted("Searching...");
        }
        if (!project_matches_)
            return;
        const auto& result = *project_matches_;
        ImGui::Text("%zu matches | %zu files | %.1f MiB%s", result.matches.size(), result.files,
                    double(result.bytes) / (1024 * 1024), result.complete() ? "" : " | Incomplete");
        if (result.cancelled)
            ImGui::TextWrapped("Search cancelled. Displayed results are partial.");
        if (result.limited)
            ImGui::TextWrapped("Search stopped at 512 matches or 32 MiB. Narrow the search text.");
        if (result.skipped)
            ImGui::TextWrapped("%zu files could not be searched.", result.skipped);
        for (const auto& error : result.errors)
            ImGui::TextWrapped("%s", error.c_str());
        if (result.matches.empty())
            return;
        ImGui::PushID(search_operation_.c_str());
        if (ImGui::BeginChild("Project search results",
                              {0, std::min(220.f, ImGui::GetContentRegionAvail().y * .4f)},
                              ImGuiChildFlags_Borders)) {
            ImGui::BeginDisabled(locked);
            for (std::size_t i = 0; i < result.matches.size(); ++i) {
                const auto& match = result.matches[i];
                ImGui::PushID(int(i));
                if (ImGui::SmallButton("Open"))
                    attempt([&] { open_search_result(project, i); });
                FORGE_UI_PROBE("cpp:project-open:" + std::to_string(i));
                ImGui::SameLine();
                ImGui::TextUnformatted((match.path + ":" + std::to_string(match.line) + ":" +
                                        std::to_string(match.column) +
                                        (match.draft ? " [open tab]" : ""))
                                           .c_str());
                ImGui::TextUnformatted(match.excerpt.c_str());
                ImGui::PopID();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    void controls(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || !request_new);
        if (button("New...", "Create a C++ source, header, Component or System through Content."))
            request_new();
        ImGui::EndDisabled();
        if (active_ && request_reveal) {
            ImGui::SameLine();
            if (button("Reveal in Content", "Find this C++ file under Content > Code."))
                request_reveal(active_->locator);
        }
        if (ImGui::TreeNode("Open by path")) {
            ImGui::BeginDisabled(locked);
            if (button("Open file", "Open a project-relative C++ source inside Native."))
                attempt([&] { open(project, locator_); });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x -
                                                        ImGui::CalcTextSize("Source path").x -
                                                        ImGui::GetStyle().ItemInnerSpacing.x));
            ImGui::InputText("Source path", locator_, sizeof(locator_));
            help(
                "Project-relative C++ source/header inside Native. Deployment files are excluded.");
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    }
};
} // namespace forge::ui
