#pragma once
#include "../asset_bytes.hpp"
#include "../json_document.hpp"
#include "../scene_asset_record.hpp"
#include "cpp_source_files.hpp"
#include "search.hpp"
#include <algorithm>
#include <forge/asset_discovery.hpp>
#include <functional>
#include <set>
#include <stop_token>

namespace forge {
// Read-only Content discovery shared with portable owner tests. Process one
// bounded JSON snapshot at a time; no second parser or retained document cache.
inline std::vector<std::filesystem::path>
scene_files(const std::filesystem::path& root, std::stop_token stop = {},
            const std::set<std::filesystem::path, ProjectLocatorLess>& excluded = {},
            const std::function<void(const std::filesystem::path&, const Json&)>& recognized = {}) {
    if (stop.stop_requested())
        throw std::runtime_error("Content scan cancelled");
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
        const auto filename = search_key(path_utf8(name));
        if (entry.is_regular_file() && search_key(path_utf8(entry.path().extension())) == ".json" &&
            filename != "forge.project.json" && filename != "forge.assets.json" &&
            !filename.ends_with(".forge-import.json") && !excluded.contains(relative)) {
            // Recognize legacy/custom .json scenes by structure, not just the extension.
            const auto bytes = entry.file_size();
            if (bytes > 8 * 1024 * 1024)
                continue;
            scanned_bytes += bytes;
            if (scanned_bytes > 64 * 1024 * 1024)
                throw std::runtime_error("Scene scan exceeds 64 MiB; use File > Open scene");
            Json candidate;
            try {
                const auto input = asset_detail::read_bytes(entry.path(), 8 * 1024 * 1024);
                candidate = detail::parse_json_document(
                    std::string_view(reinterpret_cast<const char*>(input.data()), input.size()));
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
            // Keep admission outside the parse catch: a broken recognized scene
            // must retain the last complete list, not silently disappear.
            if (recognized)
                recognized(relative, candidate);
        }
    }
    if (error)
        throw std::runtime_error("Scene scan failed: " + error.message());
    if (stop.stop_requested())
        throw std::runtime_error("Content scan cancelled");
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return search_key(path_utf8(a)) < search_key(path_utf8(b));
    });
    return result;
}
struct ContentCatalogScan {
    AssetCatalog catalog;
    std::shared_ptr<const std::vector<AssetRecord>> scenes;
    std::string diagnostic;
    std::vector<std::string> code_sources;
};
inline ContentCatalogScan
scan_content_catalog(const std::filesystem::path& root,
                     std::shared_ptr<const std::vector<AssetRecord>> previous = {},
                     std::vector<std::string> previous_code = {}, std::stop_token stop = {}) {
    ContentCatalogScan result{
        AssetCatalog::open_project(root), std::move(previous), {}, std::move(previous_code)};
    try {
        result.code_sources = ui::cpp_project_sources(root, stop);
    } catch (const std::exception& e) {
        if (stop.stop_requested())
            throw;
        result.diagnostic =
            std::string("C++ source discovery: ") + e.what() + ". Retaining the previous C++ list.";
    }
    try {
        std::set<std::filesystem::path, ProjectLocatorLess> excluded;
        for (const auto& [id, record] : result.catalog.records()) {
            (void)id;
            if (record.type != "scene")
                excluded.insert(record.source);
        }
        auto scenes = std::make_shared<std::vector<AssetRecord>>();
        (void)scene_files(root, stop, excluded, [&](const auto& path, const Json& doc) {
            if (stop.stop_requested())
                throw std::runtime_error("Content scan cancelled");
            if (!doc.contains("asset_id"))
                return;
            auto record = detail::scene_asset_record(path, doc);
            const auto known = result.catalog.records().find(record.id);
            if (known != result.catalog.records().end() &&
                (known->second.type != record.type ||
                 !ProjectPaths(root).same_locator(known->second.source, record.source)))
                throw std::runtime_error(
                    "Discovered scene identity conflicts with registered asset: " +
                    path_utf8(path));
            scenes->push_back(std::move(record));
        });
        std::sort(scenes->begin(), scenes->end(), [](const auto& a, const auto& b) {
            return search_key(path_utf8(a.source)) < search_key(path_utf8(b.source));
        });
        // Validate the complete candidate through the catalog owner once, instead
        // of rebuilding its dependency graph and comparing every previous path
        // for each discovered scene.
        AssetCatalog discovered(root);
        discovered.replace_all(*scenes);
        result.scenes = std::move(scenes);
    } catch (const std::exception& e) {
        if (stop.stop_requested())
            throw;
        if (!result.diagnostic.empty())
            result.diagnostic += "\n";
        result.diagnostic += std::string("Scene discovery: ") + e.what() +
                             ". Registered assets remain available; retaining the last complete "
                             "discovered-scene list.";
    }
    std::set<std::filesystem::path, ProjectLocatorLess> indexed_sources;
    for (const auto& [id, record] : result.catalog.records()) {
        (void)id;
        indexed_sources.insert(record.source);
    }
    std::vector<AssetRecord> combined;
    if (result.scenes)
        for (const auto& record : *result.scenes)
            if (!result.catalog.records().contains(record.id) &&
                !indexed_sources.contains(record.source))
                combined.push_back(record);
    // An unchanged discovered projection needs no registered-record copy or
    // catalog rebuild. Add registered records only when there are new additions.
    if (!combined.empty()) {
        combined.reserve(combined.size() + result.catalog.records().size());
        for (const auto& [id, record] : result.catalog.records()) {
            (void)id;
            combined.push_back(record);
        }
        result.catalog.replace_all(std::move(combined));
    }
    if (stop.stop_requested())
        throw std::runtime_error("Content scan cancelled");
    return result;
}
} // namespace forge
