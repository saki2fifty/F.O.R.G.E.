#pragma once
#include "asset_labels.hpp"
#include "search.hpp"
#include <algorithm>
#include <forge/asset_discovery.hpp>
#include <forge/assets.hpp>
#include <set>
#include <sstream>
#include <stop_token>
namespace forge {
// Read-only, disposable browser projection. Persistent identity remains in AssetCatalog.
enum class ContentState {
    Registered,
    Published,
    Unimported,
    Changed,
    Missing,
    Removed,
    Queued,
    Importing,
    Failed
};
inline const char* content_state_label(ContentState state) {
    switch (state) {
    case ContentState::Registered:
        return "Registered";
    case ContentState::Published:
        return "Published";
    case ContentState::Unimported:
        return "Not imported";
    case ContentState::Changed:
        return "Source changed";
    case ContentState::Missing:
        return "Source missing";
    case ContentState::Removed:
        return "Removed member";
    case ContentState::Queued:
        return "Queued";
    case ContentState::Importing:
        return "Importing";
    case ContentState::Failed:
        return "Import error";
    }
    return "Unknown";
}
enum class ContentEntryKind { Asset, Source, Code };
inline constexpr std::string_view code_folder_key = "@code";
inline std::string content_folder_label(const std::string& path) {
    if (path == code_folder_key)
        return "Code";
    if (path == "Code")
        return "Code (files)";
    return path_utf8(std::filesystem::u8path(path).filename());
}
struct ContentEntry {
    AssetId asset;
    std::filesystem::path source;
    std::string key, name, type, path, search;
    ContentState state = ContentState::Registered;
    std::string sort_key;
    ContentEntryKind kind = ContentEntryKind::Source;
};
struct ContentQuery {
    std::string text, type, folder;
    std::optional<ContentState> state;
    bool descendants = true;
    bool operator==(const ContentQuery&) const = default;
};
struct ContentIndex {
    std::vector<ContentEntry> entries;
    std::set<std::string> types, keys;
    // Paths are project-relative and use forward slashes; root has the empty key.
    std::map<std::string, std::vector<std::string>> folders;
    static ContentIndex build(const AssetCatalog& catalog, const SourceSnapshot* sources,
                              std::stop_token stop = {},
                              const std::map<AssetId, ContentState>& activity = {},
                              const std::vector<std::string>& code_sources = {}) {
        ContentIndex result;
        result.folders[""];
        result.folders[std::string(code_folder_key)];
        std::set<std::filesystem::path, ProjectLocatorLess> registered;
        const auto add = [&](ContentEntry entry) {
            if (stop.stop_requested())
                throw std::runtime_error("Content indexing cancelled");
            entry.path = path_utf8(entry.source);
            if (entry.kind == ContentEntryKind::Code)
                entry.path = std::string(code_folder_key) + entry.path.substr(6);
            entry.sort_key = search_key(entry.path + "/" + entry.name);
            entry.search = search_key(entry.name + " " + entry.path + " " +
                                      path_utf8(entry.source) + " " + entry.type +
                                      (entry.kind == ContentEntryKind::Code
                                           ? " code native"
                                           : " " + std::string(content_state_label(entry.state))));
            result.types.insert(entry.type);
            result.keys.insert(entry.key);
            for (auto p = std::filesystem::u8path(entry.path).parent_path(); !p.empty();
                 p = p.parent_path())
                result.folders.try_emplace(path_utf8(p));
            result.entries.push_back(std::move(entry));
        };
        for (const auto& [id, asset] : catalog.records()) {
            registered.insert(asset.source);
            auto name = content_member_name(asset);
            if (name.empty())
                name = path_utf8(asset.source.filename());
            ContentState state = ContentState::Registered;
            const AssetRecord* owner = &asset;
            if (asset.subasset) {
                const auto found = catalog.records().find(asset.subasset->owner);
                if (found != catalog.records().end())
                    owner = &found->second;
            }
            const auto imported = owner->metadata.find("forge.import");
            const bool has_import =
                imported != owner->metadata.end() && imported->is_object() &&
                imported->contains("key") && imported->at("key").is_string() &&
                valid_content_digest(imported->at("key").get_ref<const std::string&>());
            if (has_import)
                state = ContentState::Published;
            if (sources && sources->complete) {
                const auto source = sources->files.find(asset.source);
                if (source == sources->files.end())
                    state = ContentState::Missing;
                else if (has_import) {
                    const auto digest = imported->find("source_digest");
                    if (digest != imported->end() && digest->is_string() &&
                        digest->get_ref<const std::string&>() != source->second.digest)
                        state = ContentState::Changed;
                }
                if (state != ContentState::Missing)
                    for (const auto& dep : owner->source_dependencies) {
                        if (dep.revision.empty())
                            continue;
                        const auto found = sources->files.find(dep.source);
                        if (found == sources->files.end() || found->second.digest != dep.revision)
                            state = ContentState::Changed;
                    }
            }
            if (const auto job = activity.find(owner->id); job != activity.end())
                state = job->second;
            if (asset.subasset && asset.subasset->removed)
                state = ContentState::Removed;
            add({id,
                 asset.source,
                 "a:" + id.str(),
                 std::move(name),
                 asset.type,
                 {},
                 {},
                 state,
                 {},
                 ContentEntryKind::Asset});
        }
        std::set<std::string> code_paths(code_sources.begin(), code_sources.end());
        if (sources)
            for (const auto& [path, source] : sources->files) {
                if (registered.contains(path) || source.source_kind == "unrecognized" ||
                    !source.alias_of.empty() || code_paths.contains(path_utf8(path)))
                    continue;
                add({{},
                     path,
                     "s:" + path_utf8(path),
                     path_utf8(path.filename()),
                     source.source_kind,
                     {},
                     {},
                     ContentState::Unimported,
                     {},
                     ContentEntryKind::Source});
            }
        for (const auto& code : code_sources) {
            const auto path = std::filesystem::u8path(code);
            if (code.rfind("Native/", 0) != 0)
                throw std::runtime_error("C++ browser source is outside Native");
            const auto extension = search_key(path_utf8(path.extension()));
            add({{},
                 path,
                 "c:" + code,
                 path_utf8(path.filename()),
                 extension == ".cpp" || extension == ".cc" || extension == ".cxx" ? "C++ source"
                                                                                  : "C++ header",
                 {},
                 {},
                 ContentState::Registered,
                 {},
                 ContentEntryKind::Code});
        }
        for (const auto& [path, children] : result.folders) {
            (void)children;
            if (!path.empty())
                result.folders.at(path_utf8(std::filesystem::u8path(path).parent_path()))
                    .push_back(path);
        }
        std::sort(result.entries.begin(), result.entries.end(), [](const auto& a, const auto& b) {
            return a.sort_key == b.sort_key ? a.key < b.key : a.sort_key < b.sort_key;
        });
        return result;
    }
    std::vector<std::size_t> query(const ContentQuery& query) const {
        std::vector<std::string> terms;
        std::istringstream text(search_key(query.text));
        for (std::string term; text >> term;)
            terms.push_back(std::move(term));
        const auto folder = query.folder.empty() ? std::string{} : query.folder + "/";
        std::vector<std::size_t> found;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if ((!query.type.empty() && query.type != e.type) ||
                (query.state && (e.kind == ContentEntryKind::Code || *query.state != e.state)))
                continue;
            if (query.descendants
                    ? !e.path.starts_with(folder)
                    : path_utf8(std::filesystem::u8path(e.path).parent_path()) != query.folder)
                continue;
            if (std::all_of(terms.begin(), terms.end(), [&](const auto& term) {
                    return e.search.find(term) != std::string::npos;
                }))
                found.push_back(i);
        }
        return found;
    }
};
// Back/forward history is personal navigation state, never an authored document identity.
class ContentLocations {
  public:
    const std::string& current() const { return paths_[position_]; }
    bool back_available() const { return position_ != 0; }
    bool forward_available() const { return position_ + 1 < paths_.size(); }
    void visit(std::string path) {
        if (path == current())
            return;
        paths_.resize(position_ + 1);
        paths_.push_back(std::move(path));
        if (paths_.size() > 128)
            paths_.erase(paths_.begin());
        position_ = paths_.size() - 1;
    }
    void back() {
        if (back_available())
            --position_;
    }
    void forward() {
        if (forward_available())
            ++position_;
    }

  private:
    std::vector<std::string> paths_{""};
    std::size_t position_ = 0;
};
} // namespace forge
