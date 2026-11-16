#pragma once
#include "cpp_semantic_task.hpp"
#include "cpp_source_files.hpp"
#include "cpp_source_search.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include "source_text_widget.hpp"
#include "widgets.hpp"
#include <SDL3/SDL.h>
#include <TextEditor.h>
#include <cstdint>
#include <deque>
#include <imgui_internal.h>
#include <memory>
#include <utility>

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
    };
    std::filesystem::path root_;
    std::vector<std::unique_ptr<Tab>> tabs_;
    Tab* active_ = nullptr;
    struct Location {
        std::string path;
        int line = 1, column = 1;
        bool operator==(const Location&) const = default;
    };
    std::deque<Location> back_locations_, forward_locations_;
    std::uint64_t navigation_revision_ = 0;
    bool replaying_navigation_ = false;
    std::optional<Location> location() const {
        if (!active_)
            return {};
        const auto point = active_->editor.GetCursorPosition();
        return Location{active_->locator, point.mLine + 1, point.mColumn + 1};
    }
    void note_navigation(const std::optional<Location>& previous) {
        if (replaying_navigation_ || !previous || !active_ || *previous == *location())
            return;
        back_locations_.push_back(*previous);
        if (back_locations_.size() > 128)
            back_locations_.pop_front();
        forward_locations_.clear();
        ++navigation_revision_;
    }
    void activate_source(Tab& tab) {
        if (active_ == &tab)
            return;
        const auto previous = location();
        active_ = &tab;
        note_navigation(previous);
    }
    void position_source(Tab& tab, const TextEditor::Coordinates& point) {
        tab.editor.SetSelection(point, point);
        tab.editor.SetCursorPosition(point);
    }
    void select_tab_next_frame(Tab& selected) {
        for (auto& tab : tabs_)
            tab->selected = tab.get() == &selected;
    }
    template <class F> decltype(auto) with_source_tab(Tab& tab, F&& action) {
        struct Restore {
            Tab*& active;
            Tab* previous;
            ~Restore() { active = previous; }
        } restore{active_, active_};
        active_ = &tab;
        return std::forward<F>(action)();
    }
    void check_navigation(SceneDocument& project, std::uint64_t expected_revision) const {
        project.check_ownership();
        if (root_ != project.project() || expected_revision != navigation_revision_)
            throw std::runtime_error("C++ navigation changed; read its state again");
        if (close_pending())
            throw std::runtime_error("Settle the pending C++ source close request");
    }
    bool open_ = false, focus_ = false, close_all_ = false, close_popup_ = false;
    std::string close_target_, error_, search_;
    char locator_[256] = "Native/gameplay.cpp", query_[256] = "";
    std::size_t search_from_ = 0;
    Uint64 checked_disk_ = 0;
    CppSemanticTask semantic_task_;
    void source_changed(Tab& tab) {
        const auto text = tab.editor.GetText();
        tab.dirty = text != tab.saved;
        semantic_task_.observe(tab.locator, text);
    }
    CppSearchDrafts source_drafts() const {
        CppSearchDrafts drafts;
        std::size_t bytes = 0;
        for (const auto& tab : tabs_) {
            auto text = tab->editor.GetText();
            if (text.size() > cpp_search_byte_limit - bytes)
                throw std::runtime_error("Open source drafts exceed the 32 MiB budget.");
            bytes += text.size();
            drafts.emplace(std::filesystem::u8path(tab->locator), std::move(text));
        }
        return drafts;
    }
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
        semantic_task_.invalidate();
        const auto previous_location = location();
        const bool was_active = active_ && active_->locator == locator;
        std::erase_if(tabs_, [&](const auto& tab) { return tab->locator == locator; });
        if (was_active) {
            active_ = tabs_.empty() ? nullptr : tabs_.back().get();
            if (active_)
                select_tab_next_frame(*active_);
            note_navigation(previous_location);
        }
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
    std::function<CppSemanticContext()> semantic_context;
    std::function<std::string_view()> semantic_generation;
    std::function<std::string()> semantic_tool;
    void poll_semantics() {
        semantic_task_.poll();
        if (semantic_generation)
            semantic_task_.observe_generation(semantic_generation());
    }
    void start_semantics(SceneDocument& project, const std::string& path, const std::string& kind,
                         std::size_t offset, const std::string& tool = {}) {
        project.check_ownership();
        if (root_ != project.project() || !find(path))
            throw std::runtime_error("Open this project's C++ source first");
        if (!semantic_context)
            throw std::runtime_error("C++ analysis is unavailable in this editor");
        const auto provider = tool.empty() && semantic_tool ? semantic_tool() : tool;
        if (provider.empty())
            throw std::runtime_error(
                "Choose the optional clangd provider in Build > Compiler setup");
        semantic_task_.start(semantic_context(), provider, path, kind, offset, source_drafts());
    }
    void use_semantic_result(SceneDocument& project, const std::string& operation,
                             std::size_t index) {
        project.check_ownership();
        poll_semantics();
        if (project.project() != root_ || operation.empty() ||
            operation != semantic_task_.operation() || !semantic_context ||
            !semantic_task_.current(semantic_context(), source_drafts()))
            throw std::runtime_error("C++ analysis is stale or unfinished; run the query again");
        const auto& result = semantic_task_.result();
        if (result.at("kind").get<std::string>() == "signature" ||
            result.at("kind").get<std::string>() == "hover")
            throw std::runtime_error(
                "This information is read-only; choose a source edit or navigation result");
        const auto& items = result.at("items");
        if (index >= items.size() || !items.at(index).at("available").get<bool>())
            throw std::runtime_error("Choose an available C++ analysis result");
        // Own the row before any source operation can invalidate the derived result.
        const auto item = items.at(index);
        const bool format = result.at("kind").get<std::string>() == "format";
        if (result.at("kind").get<std::string>() != "completion" && !format) {
            open(project, item.at("path").get<std::string>(), item.at("line").get<int>(),
                 item.at("column").get<int>(), item.at("state").get<std::string>());
            return;
        }
        auto* tab = find(result.at("path").get<std::string>());
        if (!tab || tab->editor.IsReadOnly())
            throw std::runtime_error("Open an editable C++ source first");
        const auto before = tab->editor.GetText();
        const auto begin = item.at("begin").get<std::size_t>();
        const auto end = item.at("end").get<std::size_t>();
        const auto replacement = item.at("text").get<std::string>();
        if (begin > end || end > before.size())
            throw std::runtime_error("C++ source edit has an invalid source range");
        auto after = before;
        after.replace(begin, end - begin, replacement);
        validate_cpp_text(after);
        if (after.size() >= max_cpp_source_bytes)
            throw std::runtime_error(
                "Leave room for the source editor's final newline within 1 MiB");
        if (after == before)
            return;
        if (format) {
            const auto cursor = tab->editor.GetCursorPosition();
            const auto point = cpp_source_clamped_coordinate(before, cursor.mLine, cursor.mColumn);
            const auto offset = cpp_source_byte_offset(before, point.first, point.second);
            std::size_t source_from = 0, target_from = 0, mapped = offset;
            bool located = false;
            for (const auto& edit : result.at("format_edits")) {
                const auto first = edit.at("begin").get<std::size_t>();
                const auto last = edit.at("end").get<std::size_t>();
                if (offset < first) {
                    mapped = target_from + offset - source_from;
                    located = true;
                    break;
                }
                const auto following = target_from + first - source_from +
                                       edit.at("text").get_ref<const std::string&>().size();
                if (offset < last) {
                    mapped = following;
                    located = true;
                    break;
                }
                source_from = last;
                target_from = following;
            }
            if (!located)
                mapped = target_from + offset - source_from;
            replace_source_widget_text(tab->editor, after);
            const auto next = cpp_source_coordinate(after, std::min(mapped, after.size()));
            position_source(*tab, TextEditor::Coordinates(next.first, next.second));
        } else {
            const auto start = cpp_source_coordinate(before, begin);
            const auto finish = cpp_source_coordinate(before, end);
            replace_source_widget_text(
                tab->editor, replacement,
                std::pair{TextEditor::Coordinates(start.first, start.second),
                          TextEditor::Coordinates(finish.first, finish.second)});
        }
        source_changed(*tab);
        select_tab_next_frame(*tab);
        activate_source(*tab);
        open_ = focus_ = true;
    }
    bool is_open() const { return open_; }
    bool owns_keyboard() const { return open_ && source_widget_owns_keyboard("###C++ Sources"); }
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
        project_search_.start(root_, options, source_drafts());
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
        replace_source_widget_text(active_->editor, text);
        source_changed(*active_);
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
        source_changed(*active_);
    }
    void project_changed(const std::filesystem::path& root) {
        project_search_.cancel();
        semantic_task_.close();
        back_locations_.clear();
        forward_locations_.clear();
        replaying_navigation_ = false;
        ++navigation_revision_;
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
        const auto previous_location = location();
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
            semantic_task_.invalidate();
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
                semantic_task_.observe(tab->locator, tab->editor.GetText());
                tab->saved = tab->editor.GetText();
                tab->stamp = std::filesystem::last_write_time(path);
                tab->external = false;
            }
        }
        select_tab_next_frame(*tab);
        active_ = tab;
        open_ = focus_ = true;
        position_source(*tab,
                        TextEditor::Coordinates(std::max(line - 1, 0), std::max(column - 1, 0)));
        note_navigation(previous_location);
    }
    bool can_navigate(bool forward) const {
        return active_ && !close_pending() &&
               !(forward ? forward_locations_ : back_locations_).empty();
    }
    bool close_pending() const { return close_all_ || close_popup_ || !close_target_.empty(); }
    void navigate(SceneDocument& project, bool forward, std::uint64_t expected_revision) {
        check_navigation(project, expected_revision);
        auto& from = forward ? forward_locations_ : back_locations_;
        auto& to = forward ? back_locations_ : forward_locations_;
        if (!active_ || from.empty())
            throw std::runtime_error("No C++ source location in this direction");
        const auto previous = *location();
        const auto target = from.back();
        replaying_navigation_ = true;
        try {
            open(project, target.path, target.line, target.column);
            const auto row =
                std::clamp(target.line - 1, 0, std::max(active_->editor.GetTotalLines() - 1, 0));
            const auto point =
                cpp_source_clamped_coordinate(active_->editor.GetText(), row, target.column - 1);
            position_source(*active_, TextEditor::Coordinates(point.first, point.second));
        } catch (...) {
            replaying_navigation_ = false;
            throw;
        }
        replaying_navigation_ = false;
        from.pop_back();
        to.push_back(previous);
        ++navigation_revision_;
    }
    void clear_navigation(SceneDocument& project, std::uint64_t expected_revision) {
        check_navigation(project, expected_revision);
        if (back_locations_.empty() && forward_locations_.empty())
            return;
        back_locations_.clear();
        forward_locations_.clear();
        ++navigation_revision_;
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
        for (auto& tab : tabs_)
            if (tab->dirty)
                with_source_tab(*tab, [&] { save(project, queue_build); });
    }
    void request_close() {
        close_all_ = true;
        close_popup_ = true;
    }
    void draw(SceneDocument& project, EditorUiContext& context, bool locked,
              bool save_locked = false) {
        poll_project_search();
        poll_semantics();
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
                        const auto previous_location = location();
                        auto coordinate = [&](std::size_t end) {
                            const auto [row, col] = cpp_source_coordinate(text, end);
                            return TextEditor::Coordinates(row, col);
                        };
                        active_->editor.SetSelection(coordinate(pos),
                                                     coordinate(pos + search_.size()));
                        active_->editor.SetCursorPosition(coordinate(pos + search_.size()));
                        note_navigation(previous_location);
                        search_from_ = pos + search_.size();
                    } else
                        error_ = "No match in the active source.";
                }
            }
            draw_project_search(project, locked);
            draw_semantics(project, locked);
            if (ImGui::BeginTabBar("cpp_source_tabs")) {
                auto* requested = active_ && active_->selected ? active_ : nullptr;
                std::string closed;
                for (auto& tab : tabs_) {
                    bool tab_open = true;
                    ImGuiTabItemFlags flags =
                        tab->dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                    if (std::exchange(tab->selected, false))
                        flags |= ImGuiTabItemFlags_SetSelected;
                    if (ImGui::BeginTabItem(tab->locator.c_str(), &tab_open, flags)) {
                        // An explicit selection wins over transient fallback tabs while
                        // the tab bar lays out a rapid open or a close/replacement.
                        if (!requested || requested == tab.get()) {
                            activate_source(*tab);
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
                            if (tab->editor.HasSelection())
                                FORGE_UI_PROBE("cpp:selection");
                            if (tab->editor.IsTextChanged())
                                source_changed(*tab);
                            const auto cursor = tab->editor.GetCursorPosition();
                            ImGui::Text("Line %d, column %d | %s", cursor.mLine + 1,
                                        cursor.mColumn + 1,
                                        tab->dirty ? "Unsaved source" : "Saved source");
                            FORGE_UI_PROBE("cpp:cursor:" + std::to_string(cursor.mLine + 1) + ":" +
                                           std::to_string(cursor.mColumn + 1));
                            help("C++ highlighting, four-space tab stops, source-only Undo/Redo. "
                                 "Compiler tools run separately. Source files up to 1 MiB; no "
                                 "arbitrary C++ hot reload.");
                        }
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
                semantic_task_.invalidate();
                tabs_.clear();
                active_ = nullptr;
                open_ = false;
                close_all_ = false;
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::TextWrapped("Save changes to %s? Source history belongs to this file.",
                                   tab->locator.c_str());
                ImGui::BeginDisabled(locked || save_locked);
                if (button("Save and close",
                           "Save with external-change checking, then close this source."))
                    attempt([&] {
                        with_source_tab(*tab, [&] { save(project); });
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
                    semantic_task_.observe(tab->locator, tab->editor.GetText());
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
    void draw_semantics(SceneDocument& project, bool locked) {
        const bool expanded = ImGui::CollapsingHeader("Code assistance");
        FORGE_UI_PROBE("cpp:assistance");
        if (!expanded)
            return;
        ImGui::TextWrapped(
            "Optional clangd 23.1.0. Build Project Code once this session to prepare "
            "the compiler context. Queries use current open drafts.");
        ImGui::BeginDisabled(locked || !active_ || semantic_task_.busy());
        bool first_button = true;
        constexpr std::pair<const char*, const char*> actions[]{
            {"completion", "Complete"},        {"declaration", "Find declaration"},
            {"signature", "Signature help"},   {"diagnostics", "Check source"},
            {"hover", "Symbol info"},          {"symbols", "Symbols in file"},
            {"references", "Find references"}, {"typeDefinition", "Go to type"},
            {"format", "Format source"}};
        for (const auto& [kind, label] : actions) {
            if (!first_button)
                next_text_button(label);
            first_button = false;
            if (button(label,
                       "Analyze the current source or position without saving or changing source."))
                attempt([&] {
                    const auto cursor = active_->editor.GetCursorPosition();
                    const auto text = active_->editor.GetText();
                    start_semantics(project, active_->locator, kind,
                                    cpp_source_byte_offset(text, cursor.mLine, cursor.mColumn));
                });
            FORGE_UI_PROBE(std::string("cpp:analysis:") + kind);
        }
        ImGui::EndDisabled();
        if (semantic_task_.busy()) {
            next_text_button("Cancel analysis");
            if (button("Cancel analysis",
                       "Cancel this query through the existing task supervisor."))
                attempt([&] { semantic_task_.cancel(semantic_task_.operation()); });
            FORGE_UI_PROBE("cpp:analysis:cancel");
        }
        if (semantic_task_.stale())
            ImGui::TextWrapped("Source or compiler context changed. Run analysis again.");
        if (!semantic_task_.error().empty())
            ImGui::TextWrapped("%s", semantic_task_.error().c_str());
        const auto& result = semantic_task_.result();
        if (!result.is_null()) {
            ImGui::PushID(semantic_task_.operation().c_str());
            ImGui::TextWrapped("Results for %s",
                               result.at("path").get_ref<const std::string&>().c_str());
            FORGE_UI_PROBE("cpp:analysis:scope:" + result.at("path").get<std::string>());
            const bool signature = result.at("kind").get<std::string>() == "signature";
            const bool diagnostics = result.at("kind").get<std::string>() == "diagnostics";
            const bool information = result.at("kind").get<std::string>() == "hover";
            const bool symbols = result.at("kind").get<std::string>() == "symbols";
            const bool references = result.at("kind").get<std::string>() == "references";
            const bool type_definition = result.at("kind").get<std::string>() == "typeDefinition";
            const bool format = result.at("kind").get<std::string>() == "format";
            if (signature)
                ImGui::TextUnformatted("Call signatures (read-only)");
            if (diagnostics)
                ImGui::TextWrapped("Diagnostics received: %s",
                                   result.at("occurred_at").get_ref<const std::string&>().c_str());
            if (information)
                ImGui::TextUnformatted("Symbol information (read-only)");
            if (symbols)
                ImGui::TextUnformatted("Symbols in file");
            if (references)
                ImGui::TextWrapped(
                    "References in current context (not an exhaustive project index)");
            if (type_definition)
                ImGui::TextUnformatted("Type definitions");
            if (format)
                ImGui::TextWrapped("Formatting proposal. Source changes when you apply it.");
            ImGui::BeginChild(
                "Analysis results",
                {0, signature || diagnostics || information || symbols || references || format
                        ? 160.f
                        : 100.f},
                ImGuiChildFlags_Borders,
                format ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None);
            const auto& items = result.at("items");
            if (items.empty())
                ImGui::TextUnformatted(diagnostics
                                           ? "No diagnostics reported for this source draft."
                                       : symbols         ? "No symbols in this source draft."
                                       : references      ? "No references returned in this context."
                                       : type_definition ? "No type definition returned."
                                       : format          ? "No formatting changes returned."
                                                         : "No results at this cursor.");
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto& item = items.at(i);
                if (information) {
                    ImGui::TextWrapped(
                        "%s", item.at("documentation").get_ref<const std::string&>().c_str());
                    FORGE_UI_PROBE("cpp:analysis:info:" + std::to_string(i));
                    continue;
                }
                if (signature) {
                    ImGui::TextWrapped("%s",
                                       item.at("label").get_ref<const std::string&>().c_str());
                    const auto& active = result.at("active_signature");
                    const auto& parameter = result.at("active_parameter");
                    if (!active.is_null() && active.get<std::size_t>() == i &&
                        !parameter.is_null()) {
                        const auto index = parameter.get<std::size_t>();
                        const auto& value = item.at("parameters").at(index);
                        ImGui::TextWrapped("Argument %zu: %s", index + 1,
                                           value.at("label").get_ref<const std::string&>().c_str());
                        if (!value.at("documentation").get_ref<const std::string&>().empty())
                            ImGui::TextWrapped(
                                "%s",
                                value.at("documentation").get_ref<const std::string&>().c_str());
                    }
                    const auto& documentation =
                        item.at("documentation").get_ref<const std::string&>();
                    if (!documentation.empty())
                        ImGui::TextWrapped("%s", documentation.c_str());
                    ImGui::Separator();
                    FORGE_UI_PROBE("cpp:analysis:signature:" + std::to_string(i));
                    continue;
                }
                ImGui::PushID(static_cast<int>(i));
                if (diagnostics)
                    ImGui::TextWrapped("%s: %s",
                                       item.at("severity").get_ref<const std::string&>().c_str(),
                                       item.at("message").get_ref<const std::string&>().c_str());
                ImGui::BeginDisabled(locked || semantic_task_.stale() ||
                                     !item.at("available").get<bool>());
                const auto label =
                    symbols ? std::string(item.at("depth").get<std::size_t>() * 2, ' ') +
                                  item.at("label").get<std::string>() +
                                  (item.at("detail").get_ref<const std::string&>().empty()
                                       ? ""
                                       : " — " + item.at("detail").get<std::string>())
                    : result.at("kind").get<std::string>() == "completion" || format
                        ? item.at("label").get<std::string>()
                    : item.at("available").get<bool>()
                        ? item.at("path").get<std::string>() + ":" +
                              std::to_string(item.at("line").get<int>()) + ":" +
                              std::to_string(item.at("column").get<int>())
                        : item.value("file", std::string("Source location"));
                if (ImGui::Selectable(label.c_str()))
                    attempt([&] { use_semantic_result(project, semantic_task_.operation(), i); });
                FORGE_UI_PROBE("cpp:analysis:use:" + std::to_string(i));
                if (symbols)
                    FORGE_UI_PROBE("cpp:analysis:symbol:" + item.at("label").get<std::string>());
                ImGui::EndDisabled();
                if (format) {
                    ImGui::TextUnformatted("Proposed source");
                    const auto& preview = item.at("text").get_ref<const std::string&>();
                    ImGui::TextUnformatted(preview.data(), preview.data() + preview.size());
                    FORGE_UI_PROBE("cpp:analysis:format:" + std::to_string(i));
                }
                if (!item.at("available").get<bool>())
                    ImGui::TextWrapped("%s",
                                       item.at("reason").get_ref<const std::string&>().c_str());
                ImGui::PopID();
            }
            if (result.at("limited").get<bool>())
                ImGui::TextUnformatted(
                    "Result list is limited. Refine the source and query again.");
            ImGui::EndChild();
            ImGui::PopID();
        }
        if (!semantic_task_.log().empty() && ImGui::TreeNode("Analysis log")) {
            ImGui::TextUnformatted(semantic_task_.log().c_str());
            ImGui::TreePop();
        }
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
        for (const bool forward : {false, true}) {
            const auto label = forward ? "Forward" : "Back";
            next_text_button(label);
            ImGui::BeginDisabled(locked || !can_navigate(forward));
            if (button(label, "Return through this project's explicit C++ file/location jumps. No "
                              "source Undo or Save."))
                attempt([&] { navigate(project, forward, navigation_revision_); });
            FORGE_UI_PROBE(forward ? "cpp:navigation:forward" : "cpp:navigation:back");
            ImGui::EndDisabled();
        }
        next_text_button("Clear navigation");
        ImGui::BeginDisabled(locked || close_pending() ||
                             (back_locations_.empty() && forward_locations_.empty()));
        if (button("Clear navigation", "Clear only the source Back/Forward locations. Source Undo "
                                       "and files are retained."))
            attempt([&] { clear_navigation(project, navigation_revision_); });
        FORGE_UI_PROBE("cpp:navigation:clear");
        ImGui::EndDisabled();
        if (active_ && request_reveal) {
            next_text_button("Reveal in Content");
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
