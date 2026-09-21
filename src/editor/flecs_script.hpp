#pragma once
#include "document.hpp"
#include "document_workspace.hpp"
#include "widgets.hpp"
#include <forge/flecs_script.hpp>
#include <fstream>
#include <future>
namespace forge::ui {
class FlecsScriptEditor {
  public:
    bool pending() const { return job_.valid(); }
    explicit FlecsScriptEditor(std::filesystem::path executable)
        : executable_(std::move(executable)), text_(1024 * 1024 + 1, 0) {}
    ~FlecsScriptEditor() { cancel_.request_stop(); }
    bool is_open() const { return open_; }
    bool dirty() const { return open_ && std::string(text_.data()) != baseline_; }
    bool close_cancelled = false;
    void navigate_source(SceneDocument& project, const std::string& source, int line = 0,
                         int column = 0) {
        const auto locator = ProjectPaths::normalize(std::filesystem::u8path(source));
        const auto path = ProjectPaths(project.project()).resolve(locator);
        if (locator.extension() != ".flecs" || std::filesystem::file_size(path) > 1024 * 1024)
            throw std::runtime_error(
                "Choose a project-contained Flecs Script source of at most 1 MiB");
        auto offset = [&](const std::string& text) {
            std::size_t pos = 0;
            for (int row = 1; row < line && pos < text.size(); ++row) {
                const auto end = text.find('\n', pos);
                pos = end == std::string::npos ? text.size() : end + 1;
            }
            const auto end = text.find('\n', pos);
            return int(std::min(end == std::string::npos ? text.size() : end,
                                pos + std::size_t(std::max(column - 1, 0))));
        };
        if (open_ && root_ == project.project() && locator == record_.source) {
            select_start_ = offset(text_.data());
            select_end_ = select_start_;
            focus_ = true;
            return;
        }
        const auto text = read_flecs_script_source(project.project(), locator);
        source_view_.assign(text.begin(), text.end());
        source_view_.push_back(0);
        source_view_offset_ = offset(text);
        source_view_name_ = path_utf8(locator);
        source_view_root_ = project.project();
        source_view_open_ = true;
    }
    void draw_source_viewer(SceneDocument& project) {
        if (source_view_root_ != project.project())
            source_view_open_ = false;
        if (!source_view_open_)
            return;
        ImGui::SetNextWindowSize({700, 500}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Script diagnostic source", &source_view_open_)) {
            ImGui::TextWrapped("%s (read only)", source_view_name_.c_str());
            help("Source navigation preserves your active draft. Register/open this source from "
                 "Content to edit it.");
            if (source_view_offset_ >= 0)
                ImGui::SetKeyboardFocusHere();
            ImGui::InputTextMultiline(
                "##diagnostic-source", source_view_.data(), source_view_.size(), {-1, -1},
                ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways,
                [](ImGuiInputTextCallbackData* data) {
                    auto& offset = *static_cast<int*>(data->UserData);
                    if (offset >= 0) {
                        data->CursorPos = data->SelectionStart = data->SelectionEnd = offset;
                        offset = -1;
                    }
                    return 0;
                },
                &source_view_offset_);
            help("Read-only source at the reported location when structured coordinates are "
                 "available.");
        }
        ImGui::End();
    }
    void request_close() {
        if (dirty())
            close_requested_ = true;
        else {
            open_ = false;
            cancel_.request_stop();
        }
    }
    void request_save() { save_requested_ = true; }
    void open(SceneDocument& project, const AssetRecord& record) {
        if (dirty())
            throw std::runtime_error(
                "Save or close the current Flecs Script draft before opening another script");
        cancel_.request_stop();
        if (job_.valid())
            throw std::runtime_error("Wait for the previous script preview to finish cancelling");
        auto baseline = read_flecs_script_source(project.project(), record.source);
        root_ = project.project();
        record_ = record;
        baseline_.swap(baseline);
        std::copy(baseline_.begin(), baseline_.end(), text_.begin());
        text_[baseline_.size()] = 0;
        previous_.clear();
        result_.clear();
        error_.clear();
        error_source_.clear();
        error_line_ = error_column_ = 0;
        open_ = true;
        focus_ = true;
    }
    void content(SceneDocument& project, bool locked, EditorSelection& selection,
                 std::string& message) {
        if (!ImGui::CollapsingHeader("Flecs Script")) {
            help("Create or register a project-contained .flecs source file.");
            return;
        }
        help("Flecs Script has its own source editor and managed preview world. It is distinct "
             "from native gameplay and future visual scripting.");
        ImGui::InputText("Script path", source_, sizeof(source_));
        help("Project-relative .flecs file, such as Assets/example.flecs.");
        ImGui::BeginDisabled(locked);
        if (button("Create / Register Script",
                   "Create an example if the file is absent; otherwise register the existing "
                   "source without overwriting it.")) {
            try {
                project.check_ownership();
                ProjectPaths paths(project.project());
                const auto locator = ProjectPaths::normalize(std::filesystem::u8path(source_));
                if (locator.extension() != ".flecs")
                    throw std::runtime_error("Script path must end in .flecs");
                const auto file = paths.resolve(locator);
                if (!std::filesystem::exists(file))
                    atomic_write(file, "using flecs.meta\n\nstruct Position { x { member: {f32} } "
                                       "y { member: {f32} } }\nExample { Position: {10, 20} }\n");
                const auto record = register_flecs_script(project.project(), locator);
                selection.select_asset(record.id);
                open(project, record);
                message = "Flecs Script registered. Apply runs its managed preview in a worker.";
            } catch (const std::exception& e) {
                error_ = message = e.what();
            }
        }
        ImGui::EndDisabled();
    }
    void poll(SceneDocument& project, Problems& problems) {
        if (!job_.valid())
            return;
        if (project.project() != root_)
            cancel_.request_stop();
        if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            auto result = job_.get();
            error_source_ = result.value("source", path_utf8(record_.source));
            error_line_ = result.value("line", 0);
            error_column_ = result.value("column", 0);
            if (cancel_.stop_requested())
                throw std::runtime_error("Script preview cancelled");
            if (!publish_flecs_script_preview(result, submitted_, previous_, result_)) {
                error_ = result.at("error");
                const int line = result.value("line", 0), column = result.value("column", 0);
                if (line)
                    error_ = "Line " + std::to_string(line) + ", column " + std::to_string(column) +
                             ": " + error_;
                throw std::runtime_error(error_);
            }
            error_.clear();
            problems.resolve(problem_key());
        } catch (const std::exception& e) {
            error_ = e.what();
            problems.report({problem_key(),
                             "Error",
                             error_,
                             {},
                             error_source_,
                             {},
                             record_.id,
                             error_line_,
                             error_column_,
                             true});
        }
    }
    void draw(SceneDocument& project, bool locked) {
        if (!open_)
            return;
        if (root_ != project.project()) {
            request_close();
            return;
        }
        draft_window_size({850, 650});
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        bool visible = true;
        const auto title = std::string(dirty() ? "* " : "") + record_.source.filename().string() +
                           "###Flecs Script";
        const bool expanded = ImGui::Begin(title.c_str(), &visible);
        if (!visible)
            request_close();
        if (editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            editor_context->task.focus_document("flecs_script", "Flecs Script");
        if (save_requested_ && !locked) {
            save_requested_ = false;
            save(project);
        }
        if (expanded) {
            ImGui::TextWrapped("%s | Isolated managed Preview world",
                               path_utf8(record_.source).c_str());
            help("Save owns this source file. Apply evaluates a candidate in a worker; the "
                 "previous successful preview remains on failure. Scene Undo does not edit script "
                 "source or undo evaluation.");
            ImGui::BeginDisabled(locked);
            if (button("Save",
                       "Save the source file with conflict detection; does not evaluate it."))
                save(project);
            ImGui::SameLine();
            ImGui::BeginDisabled(job_.valid());
            if (button("Apply / Reload", "Evaluate this draft in a bounded worker using native "
                                         "managed-script update semantics."))
                apply(false);
            ImGui::SameLine();
            if (button("Fresh preview", "Evaluate without the previous managed source, for a "
                                        "deliberately fresh preview session."))
                apply(true);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (job_.valid()) {
                ImGui::SameLine();
                if (button("Cancel",
                           "Terminate the candidate worker; keep the last successful preview."))
                    cancel_.request_stop();
            }
            ImGui::InputTextWithHint("##find", "Find in script...", search_, sizeof(search_));
            help("Search source text. Find next selects the matching text in the editor.");
            ImGui::SameLine();
            if (button("Find next", "Find the next occurrence, wrapping to the beginning.")) {
                const std::string source = text_.data(), term = search_;
                if (!term.empty()) {
                    auto pos = source.find(term, search_position_);
                    if (pos == std::string::npos)
                        pos = source.find(term);
                    if (pos != std::string::npos) {
                        search_position_ = pos + term.size();
                        select_start_ = int(pos);
                        select_end_ = int(search_position_);
                    }
                }
            }
            if (select_start_ >= 0)
                ImGui::SetKeyboardFocusHere();
            ImGui::BeginDisabled(locked);
            ImGui::InputTextMultiline(
                "##source", text_.data(), text_.size(),
                {-1, std::max(160.f, ImGui::GetContentRegionAvail().y * .62f)},
                ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
                [](ImGuiInputTextCallbackData* data) {
                    auto& editor = *static_cast<FlecsScriptEditor*>(data->UserData);
                    if (editor.select_start_ >= 0) {
                        data->SelectionStart = editor.select_start_;
                        data->SelectionEnd = data->CursorPos = editor.select_end_;
                        editor.select_start_ = -1;
                    }
                    return 0;
                },
                this);
            ImGui::EndDisabled();
            help("Flecs Script source. Native text Undo/Redo applies inside this field. Save and "
                 "evaluation are separate operations.");
            if (!error_.empty())
                field_error(error_.c_str());
            if (!error_.empty() && !error_source_.empty() &&
                button("Go to error",
                       "Navigate to this diagnostic's source without discarding your draft.")) {
                try {
                    navigate_source(project, error_source_, error_line_, error_column_);
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            if (ImGui::BeginChild("managed-output", {0, 0}, ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_HorizontalScrollbar))
                ImGui::TextUnformatted(result_.empty()
                                           ? "Apply to inspect generated ECS content. This preview "
                                             "does not alter the open scene."
                                           : result_.c_str());
            ImGui::EndChild();
        }
        ImGui::End();
        if (close_requested_)
            ImGui::OpenPopup("Unsaved Flecs Script");
        if (ImGui::BeginPopupModal("Unsaved Flecs Script", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save changes to this script source before closing?");
            if (button("Save and close", "Save the file; scene history is independent.") &&
                save(project))
                finish_close();
            ImGui::SameLine();
            if (button("Discard", "Discard only this unsaved script draft."))
                finish_close();
            ImGui::SameLine();
            if (button("Cancel", "Keep editing and cancel the pending close or project switch.")) {
                close_requested_ = false;
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

  private:
    std::filesystem::path executable_, root_;
    AssetRecord record_;
    std::vector<char> text_;
    std::string baseline_, previous_, submitted_, result_, error_;
    std::string error_source_, source_view_name_;
    std::filesystem::path source_view_root_;
    std::vector<char> source_view_;
    int error_line_ = 0, error_column_ = 0, source_view_offset_ = -1;
    bool source_view_open_ = false;
    char source_[1024] = "Assets/example.flecs", search_[256]{};
    std::size_t search_position_ = 0;
    int select_start_ = -1, select_end_ = 0;
    bool open_ = false, focus_ = false, close_requested_ = false, save_requested_ = false;
    std::stop_source cancel_;
    std::future<Json> job_;
    std::string problem_key() const { return "script/" + record_.id.str(); }
    bool save(SceneDocument& project) {
        try {
            project.check_ownership();
            if (root_ != project.project())
                throw std::runtime_error("Script belongs to another project");
            const auto file = ProjectPaths(root_).resolve(record_.source);
            const auto current = read_flecs_script_source(root_, record_.source);
            if (current != baseline_)
                throw std::runtime_error("Script changed on disk; preserve this draft and reopen "
                                         "the source before overwriting");
            atomic_write(file, text_.data());
            baseline_ = text_.data();
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void apply(bool fresh) {
        error_source_ = path_utf8(record_.source);
        error_line_ = error_column_ = 0;
        submitted_ = text_.data();
        cancel_ = std::stop_source{};
        auto root = root_, source = record_.source, tool = executable_;
        auto code = submitted_, previous = fresh ? std::string{} : previous_;
        auto cancel = cancel_.get_token();
        job_ = std::async(std::launch::async, [root, source, tool, code, previous, cancel] {
            return preview_flecs_script(root, source, code, previous, tool, cancel);
        });
    }
    void finish_close() {
        close_requested_ = false;
        open_ = false;
        cancel_.request_stop();
        ImGui::CloseCurrentPopup();
    }
};
} // namespace forge::ui
