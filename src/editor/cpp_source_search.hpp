#pragma once
#include "../asset_bytes.hpp"
#include "../utf8_boundary.hpp"
#include "cpp_source_files.hpp"
#include <chrono>
#include <future>
#include <map>
#include <set>
#include <stop_token>
#include <tuple>

namespace forge::ui {
struct CppSourceChanged : std::runtime_error {
    CppSourceChanged()
        : std::runtime_error("Source changed since this search/read. Search again.") {}
};
struct CppSearchOptions {
    std::string query;
    bool match_case = true, whole_word = false;
};
struct CppSearchMatch {
    std::string path, state, excerpt;
    std::size_t offset{}, length{};
    int line = 1, column = 1;
    bool draft = false;
};
struct CppSearchResult {
    std::vector<CppSearchMatch> matches;
    std::vector<std::string> errors;
    std::size_t files{}, bytes{}, skipped{};
    bool cancelled = false, limited = false;
    bool complete() const { return !cancelled && !limited && !skipped && errors.empty(); }
};
inline constexpr std::size_t cpp_search_byte_limit = 32 * 1024 * 1024;
inline constexpr std::size_t cpp_search_match_limit = 512;
using CppSearchDrafts = std::map<std::filesystem::path, std::string, ProjectLocatorLess>;
inline void validate_cpp_search(const CppSearchOptions& options) {
    validate_cpp_text(options.query);
    if (options.query.empty() || options.query.size() > 255 ||
        options.query.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("Find in Project needs 1 to 255 UTF-8 bytes on one line.");
}
// Byte offsets remain suitable for source digests; columns use the source
// widget's four-space tabs and one column per Unicode code point.
inline std::pair<int, int> cpp_source_coordinate(std::string_view text, std::size_t end,
                                                 std::size_t begin = 0, int row = 0, int col = 0) {
    for (std::size_t i = begin; i < std::min(end, text.size()); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            ++row;
            col = 0;
        } else if (c == '\t')
            col = (col / 4 + 1) * 4;
        else if (c != '\r' && (c & 0xc0) != 0x80)
            ++col;
    }
    return {row, col};
}
inline std::string cpp_source_state(std::string_view text) {
    return asset_detail::content_digest(std::as_bytes(std::span(text.data(), text.size())));
}
inline CppSearchResult search_cpp_sources(const std::filesystem::path& root,
                                          const CppSearchOptions& options,
                                          const CppSearchDrafts& drafts = {},
                                          std::stop_token stop = {}) {
    validate_cpp_search(options);
    CppSearchResult result;
    const auto fold = [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); };
    const auto word = [](unsigned char c) {
        return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    std::string query = options.query;
    if (!options.match_case)
        std::transform(query.begin(), query.end(), query.begin(), fold);
    try {
        std::set<std::filesystem::path, ProjectLocatorLess> paths;
        for (const auto& path : cpp_project_sources(root, stop))
            paths.insert(std::filesystem::u8path(path));
        // An open draft remains searchable if its disk file was deleted. The
        // normal source owner still refuses to save over that external change.
        for (const auto& [path, text] : drafts) {
            (void)text;
            (void)cpp_source_path(root, path);
            paths.insert(path);
        }
        if (paths.size() > 2048)
            throw std::runtime_error("Too many project C++ source files.");
        for (const auto& path : paths) {
            if (stop.stop_requested()) {
                result.cancelled = true;
                break;
            }
            try {
                const auto draft = drafts.find(path);
                const bool open = draft != drafts.end();
                const auto file = cpp_source_path(root, path);
                const auto text = open ? draft->second : read_cpp_source(file);
                validate_cpp_text(text);
                if (text.size() > cpp_search_byte_limit - result.bytes) {
                    result.limited = true;
                    break;
                }
                result.bytes += text.size();
                ++result.files;
                std::string folded;
                if (!options.match_case) {
                    folded = text;
                    std::transform(folded.begin(), folded.end(), folded.begin(), fold);
                }
                const std::string_view haystack = options.match_case ? text : folded;
                std::string state;
                std::size_t scan = 0, coordinate_from = 0;
                int row = 0, column = 0;
                // Chunk the candidate starts so cancellation never waits for a
                // complete 1 MiB repeated-text search.
                while (scan < haystack.size()) {
                    if (stop.stop_requested()) {
                        result.cancelled = true;
                        return result;
                    }
                    const auto starts_end = std::min(scan + 4096, haystack.size());
                    const auto window_end =
                        std::min(starts_end + query.size() - 1, haystack.size());
                    const auto found = haystack.substr(scan, window_end - scan).find(query);
                    if (found == std::string_view::npos || scan + found >= starts_end) {
                        scan = starts_end;
                        continue;
                    }
                    const auto offset = scan + found, end = offset + query.size();
                    scan = end;
                    if (options.whole_word &&
                        ((offset && word(static_cast<unsigned char>(text[offset - 1]))) ||
                         (end < text.size() && word(static_cast<unsigned char>(text[end]))))) {
                        scan = offset + 1;
                        continue;
                    }
                    if (state.empty())
                        state = cpp_source_state(text);
                    std::tie(row, column) =
                        cpp_source_coordinate(text, offset, coordinate_from, row, column);
                    coordinate_from = offset;
                    const auto line_begin = text.rfind('\n', offset);
                    auto begin = line_begin == std::string::npos ? 0 : line_begin + 1;
                    begin = std::max(begin, offset > 80 ? offset - 80 : 0);
                    while (begin < offset &&
                           (static_cast<unsigned char>(text[begin]) & 0xc0) == 0x80)
                        ++begin;
                    auto line_end = text.find_first_of("\r\n", offset);
                    if (line_end == std::string::npos)
                        line_end = text.size();
                    const auto excerpt_end =
                        detail::utf8_range_end(text, begin, std::min(line_end, begin + 340));
                    result.matches.push_back({path_utf8(path), state,
                                              text.substr(begin, excerpt_end - begin), offset,
                                              query.size(), row + 1, column + 1, open});
                    if (result.matches.size() == cpp_search_match_limit) {
                        result.limited = true;
                        return result;
                    }
                }
            } catch (const std::exception& error) {
                ++result.skipped;
                if (result.errors.size() < 16)
                    result.errors.push_back(path_utf8(path) + ": " + error.what());
            }
        }
    } catch (const std::exception& error) {
        if (stop.stop_requested())
            result.cancelled = true;
        else
            result.errors.push_back(error.what());
    }
    return result;
}
// CPU-only task borrowed by the source editor. It owns no source/history state.
class CppSourceSearch {
    std::stop_source stop_;
    std::future<CppSearchResult> future_;

  public:
    ~CppSourceSearch() { cancel(); }
    bool busy() const { return future_.valid(); }
    void cancel() { stop_.request_stop(); }
    void start(std::filesystem::path root, CppSearchOptions options, CppSearchDrafts drafts) {
        validate_cpp_search(options);
        if (busy())
            throw std::runtime_error("Wait for the current project search or cancel it first.");
        std::size_t bytes = 0;
        for (const auto& [path, text] : drafts) {
            (void)path;
            if (text.size() > cpp_search_byte_limit - bytes)
                throw std::runtime_error("Open source drafts exceed the 32 MiB search budget.");
            bytes += text.size();
        }
        stop_ = std::stop_source{};
        future_ =
            std::async(std::launch::async, [root = std::move(root), options = std::move(options),
                                            drafts = std::move(drafts), stop = stop_.get_token()] {
                return search_cpp_sources(root, options, drafts, stop);
            });
    }
    std::optional<CppSearchResult> poll() {
        if (!future_.valid() ||
            future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return {};
        auto result = future_.get();
        result.cancelled = result.cancelled || stop_.stop_requested();
        return result;
    }
};
} // namespace forge::ui
