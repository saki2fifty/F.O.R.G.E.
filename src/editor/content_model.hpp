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
struct ContentEntry {
    AssetId asset;
    std::filesystem::path source;
    std::string key, name, type, path, search;
    ContentState state = ContentState::Registered;
    std::string sort_key;
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
                              const std::map<AssetId, ContentState>& activity = {}) {
        ContentIndex result;
        result.folders[""];
        std::set<std::filesystem::path, ProjectLocatorLess> registered;
        const auto add = [&](ContentEntry entry) {
            if (stop.stop_requested())
                throw std::runtime_error("Content indexing cancelled");
            entry.path = path_utf8(entry.source);
            entry.sort_key = search_key(entry.path + "/" + entry.name);
            entry.search = search_key(entry.name + " " + entry.path + " " + entry.type + " " +
                                      content_state_label(entry.state));
            result.types.insert(entry.type);
            result.keys.insert(entry.key);
            for (auto p = entry.source.parent_path(); !p.empty(); p = p.parent_path())
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
            add({id, asset.source, "a:" + id.str(), std::move(name), asset.type, {}, {}, state});
        }
        if (sources)
            for (const auto& [path, source] : sources->files) {
                if (registered.contains(path) || source.source_kind == "unrecognized" ||
                    !source.alias_of.empty())
                    continue;
                add({{},
                     path,
                     "s:" + path_utf8(path),
                     path_utf8(path.filename()),
                     source.source_kind,
                     {},
                     {},
                     ContentState::Unimported});
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
                (query.state && *query.state != e.state))
                continue;
            if (query.descendants ? !e.path.starts_with(folder)
                                  : path_utf8(e.source.parent_path()) != query.folder)
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
