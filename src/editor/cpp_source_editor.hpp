#pragma once
#include "cpp_source_files.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include "widgets.hpp"
#include <TextEditor.h>
#include <cstdint>
#include <memory>

namespace forge::ui {
class CppSourceEditor {
    struct Tab {
        std::string locator, disk, saved;
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
    bool open_ = false, close_all_ = false, close_popup_ = false;
    std::string close_target_, error_, search_;
    char locator_[256] = "Native/gameplay.cpp", new_name_[128] = "behavior.cpp", query_[256] = "";
    std::size_t search_from_ = 0;
    Uint64 checked_disk_ = 0;
    Tab* find(const std::string& locator) {
        ProjectPaths paths(root_);
        for (auto& tab : tabs_)
            if (paths.same_locator(tab->locator, locator))
                return tab.get();
        return nullptr;
    }
    void erase(const std::string& locator) {
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
    std::function<void()> request_build;
    bool is_open() const { return open_; }
    std::string active_source() const { return active_ ? active_->locator : std::string{}; }
    std::string active_text() const { return active_ ? active_->editor.GetText() : std::string{}; }
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
        root_ = root;
        tabs_.clear();
        active_ = nullptr;
        open_ = false;
        close_all_ = close_popup_ = build_pending = false;
        error_.clear();
    }
    void open(SceneDocument& project, const std::string& locator, int line = 1, int column = 1) {
        if (root_ != project.project())
            project_changed(project.project());
        const auto normalized =
            path_utf8(ProjectPaths::normalize(std::filesystem::u8path(locator)));
        auto* tab = find(normalized);
        if (!tab) {
            auto path = cpp_source_path(root_, normalized);
            if (tabs_.size() >= 32)
                throw std::runtime_error("Close a source tab before opening more than 32 files.");
            tabs_.push_back(std::make_unique<Tab>(normalized, read_cpp_source(path)));
            tab = tabs_.back().get();
            tab->stamp = std::filesystem::last_write_time(path);
        }
        if (!tab->dirty) {
            const auto path = cpp_source_path(root_, normalized);
            const auto disk = read_cpp_source(path);
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
        open_ = true;
        tab->editor.SetCursorPosition(
            TextEditor::Coordinates(std::max(line - 1, 0), std::max(column - 1, 0)));
    }
    void save(SceneDocument& project) {
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
        if (build_on_save)
            build_pending = true;
    }
    void request_close() {
        close_all_ = true;
        close_popup_ = true;
    }
    void draw(SceneDocument& project, EditorUiContext& context, bool locked,
              bool save_locked = false) {
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
        const bool contents = ImGui::Begin("C++ Sources###C++ Sources", &visible);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            context.task.focus_document("cpp_sources", "C++ Sources");
        if (!visible)
            request_close();
        if (contents) {
            controls(project, locked || save_locked);
            ImGui::BeginDisabled(locked || save_locked);
            if (button("Save source", "Save only the active source file; scene Save is separate."))
                attempt([&] { save(project); });
            ImGui::SameLine();
            if (button("Reload from disk",
                       "Discard this source draft and source history after confirmation.")) {
                if (active_) {
                    close_target_ = active_->locator;
                    ImGui::OpenPopup("Reload C++ source?");
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Build on Save", &build_on_save) && !build_on_save)
                build_pending = false;
            FORGE_UI_PROBE("cpp:build-on-save");
            help("Queue a managed gameplay build after saving. Waits until Play stops and all "
                 "source drafts are saved.");
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!build_enabled || !request_build);
            if (button("Build gameplay", "Build all saved C++ gameplay sources using the existing "
                                         "isolated compiler task. Stop Play first."))
                attempt([&] { request_build(); });
            ImGui::EndDisabled();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F) &&
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
                            int row = 0, col = 0;
                            for (std::size_t i = 0; i < end; ++i) {
                                const auto c = static_cast<unsigned char>(text[i]);
                                if (c == '\n') {
                                    ++row;
                                    col = 0;
                                } else if (c == '\t')
                                    col = (col / 4 + 1) * 4;
                                else if ((c & 0xc0) != 0x80)
                                    ++col;
                            }
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
                        const auto available = ImGui::GetContentRegionAvail();
                        tab->editor.Render("C++ text",
                                           {available.x, std::max(80.0f, available.y - 35.0f)});
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
            if (!locked && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
                ImGui::IsKeyPressed(ImGuiKey_Z) &&
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                undo(true);
            if (!locked && !save_locked && ImGui::GetIO().KeyCtrl &&
                ImGui::IsKeyPressed(ImGuiKey_S) &&
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
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
    void controls(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked);
        ImGui::InputText("Source path", locator_, sizeof(locator_));
        help("Project-relative C++ source/header inside Native. Deployment files are excluded.");
        if (button("Open file", "Open the source in FORGE with its own Save and Undo history."))
            attempt([&] { open(project, locator_); });
        ImGui::InputText("New source filename", new_name_, sizeof(new_name_));
        FORGE_UI_PROBE("cpp:new-filename");
        help("A new .cpp or .hpp directly inside Native; existing files are never overwritten.");
        if (button("Create source file", "Create and open a managed source; .cpp is explicitly "
                                         "added to gameplay compilation."))
            attempt([&] {
                project.check_ownership();
                create_cpp_source(project.project(), new_name_);
                open(project, "Native/" + std::string(new_name_));
            });
        ImGui::EndDisabled();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    }
};
} // namespace forge::ui
