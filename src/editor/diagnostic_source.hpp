#pragma once
#include "../asset_bytes.hpp"
#include "editor_state.hpp"
#include <charconv>
#include <forge/project_paths.hpp>
#include <utility>

namespace forge::ui {
inline bool diagnostic_text_source(const std::filesystem::path& source) {
    const auto extension = source.extension();
    return extension == ".json" || extension == ".hlsl" || extension == ".hlsli" ||
           extension == ".fx" || extension == ".fxh" || extension == ".glsl" ||
           extension == ".wgsl" || extension == ".flecs";
}
// Coordinates are presentation hints only. A compiler message never authorizes
// reading outside the project; the viewer rechecks containment on every open.
inline bool shader_diagnostic_location(Problem& problem, const std::filesystem::path& root,
                                       const std::filesystem::path& source_root) {
    std::string_view text(problem.text.data(), std::min(problem.text.size(), std::size_t(65536)));
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto close = line.find("): error");
        if (close == std::string_view::npos)
            continue;
        const auto open = line.rfind('(', close);
        if (open == std::string_view::npos || open == 0)
            continue;
        int row = 0, column = 1;
        const auto coordinate = line.substr(open + 1, close - open - 1);
        const auto [next, error] =
            std::from_chars(coordinate.data(), coordinate.data() + coordinate.size(), row);
        if (error != std::errc{} || row < 1)
            continue;
        if (next != coordinate.data() + coordinate.size()) {
            if (*next != ',')
                continue;
            const auto [last, column_error] =
                std::from_chars(next + 1, coordinate.data() + coordinate.size(), column);
            if (column_error != std::errc{} || column < 1 ||
                (last != coordinate.data() + coordinate.size() && *last != '-'))
                continue;
        }
        try {
            auto file = line.substr(0, open);
            while (!file.empty() && (file.front() == ' ' || file.front() == '\t'))
                file.remove_prefix(1);
            const auto virtual_file = ProjectPaths::normalize(std::filesystem::u8path(file));
            const auto locator = ProjectPaths::normalize(source_root / virtual_file);
            if (!diagnostic_text_source(locator))
                continue;
            (void)ProjectPaths(root).resolve(locator);
            problem.source = path_utf8(locator);
            problem.line = row;
            problem.column = column;
            problem.source_navigation = true;
            return true;
        } catch (const std::exception&) {
            // Preserve the original diagnostic even when its location is unsafe.
        }
    }
    return false;
}
class DiagnosticSourceViewer {
  public:
    void open(const std::filesystem::path& root, const Problem& problem) {
        const auto locator = ProjectPaths::normalize(std::filesystem::u8path(problem.source));
        if (!diagnostic_text_source(locator))
            throw std::runtime_error("This diagnostic does not identify a supported text source");
        const auto bytes =
            asset_detail::read_bytes(ProjectPaths(root).resolve(locator), 1024 * 1024);
        if (std::find(bytes.begin(), bytes.end(), std::byte{}) != bytes.end())
            throw std::runtime_error("Diagnostic source contains binary data");
        std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::size_t offset = 0;
        for (int line = 1; line < problem.line && offset < text.size(); ++line) {
            const auto end = text.find('\n', offset);
            offset = end == std::string::npos ? text.size() : end + 1;
        }
        const auto end = text.find('\n', offset);
        offset = std::min(end == std::string::npos ? text.size() : end,
                          offset + std::size_t(std::max(problem.column, 1) - 1));
        text_.assign(text.begin(), text.end());
        text_.push_back(0);
        offset_ = int(offset);
        root_ = root;
        name_ = path_utf8(locator);
        open_ = focus_ = true;
    }
    void draw(const std::filesystem::path& project) {
        if (project != root_)
            open_ = false;
        if (!open_)
            return;
        ImGui::SetNextWindowSize({800, 550}, ImGuiCond_FirstUseEver);
        if (std::exchange(focus_, false))
            ImGui::SetNextWindowFocus();
        if (ImGui::Begin("Diagnostic source", &open_)) {
            ImGui::TextWrapped("%s (read only)", name_.c_str());
            help("Source at the reported location. Existing asset and script drafts remain open "
                 "and unchanged; use their editor to make changes.");
            if (offset_ >= 0)
                ImGui::SetKeyboardFocusHere();
            ImGui::InputTextMultiline(
                "##source", text_.data(), text_.size(), {-1, -1},
                ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways,
                [](ImGuiInputTextCallbackData* data) {
                    auto& offset = *static_cast<int*>(data->UserData);
                    if (offset >= 0) {
                        data->CursorPos = data->SelectionStart = data->SelectionEnd = offset;
                        offset = -1;
                    }
                    return 0;
                },
                &offset_);
            help("Read-only text; the caret starts at the diagnostic line and column when known.");
        }
        ImGui::End();
    }
    const std::string& source() const { return name_; }
    int requested_offset() const { return offset_; }

  private:
    std::filesystem::path root_;
    std::string name_;
    std::vector<char> text_;
    int offset_ = -1;
    bool open_ = false, focus_ = false;
};
} // namespace forge::ui
