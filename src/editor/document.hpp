#pragma once
#include <algorithm>
#include <cctype>
#include <forge/prefab_authoring.hpp>
#include <forge/project.hpp>
#include <forge/project_lease.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <optional>
namespace forge {
inline std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
inline Json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot read " + path_text(path));
    Json result;
    input >> result;
    return result;
}
inline std::filesystem::path project_file(const std::filesystem::path& root,
                                          const std::filesystem::path& path) {
    const ProjectPaths paths(root);
    const auto resolved =
        path.is_absolute() ? paths.resolve(paths.relative(path)) : paths.resolve(path);
    const auto relative = paths.relative(resolved);
    auto first_part = relative.empty() ? std::string{} : path_text(*relative.begin());
    auto relative_text = path_text(relative);
#ifdef _WIN32
    std::transform(first_part.begin(), first_part.end(), first_part.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(relative_text.begin(), relative_text.end(), relative_text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (relative_text.find(':') != std::string::npos)
        throw std::runtime_error("Scene paths cannot contain alternate data streams");
#endif
    if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
        first_part == ".forge" || relative_text == "forge.project.json" ||
        resolved.extension() != ".json")
        throw std::runtime_error("Choose a JSON scene file inside the current project");
    return resolved;
}
inline std::filesystem::path project_control_file(const std::filesystem::path& root,
                                                  const std::filesystem::path& relative) {
    const auto path = root / ".forge" / relative;
    if (std::filesystem::weakly_canonical(path) != path)
        throw std::runtime_error("Project control file must not redirect to another location");
    return path;
}
class SceneDocument {
  public:
    explicit SceneDocument(Scene& scene) : scene_(scene) {}
    PrefabLibrary& prefabs() {
        if (!prefabs_)
            throw std::runtime_error("No prefab project");
        return *prefabs_;
    }
    ProjectSettings& settings() {
        if (!settings_)
            throw std::runtime_error("No project settings");
        return *settings_;
    }
    void save_settings(Json candidate, const Json* expected = nullptr) {
        check_ownership();
        settings().save(std::move(candidate), expected);
    }
    const std::filesystem::path& project() const { return root_; }
    const std::filesystem::path& path() const { return path_; }
    const std::string& name() const { return name_; }
    std::uint64_t generation() const { return generation_; }
    void check_ownership() const {
        if (!lease_)
            throw std::runtime_error("No project writer ownership");
        lease_->check();
    }
    std::shared_ptr<const ProjectLease> writer_guard() const {
        check_ownership();
        return lease_;
    }
    bool on_disk() const { return persisted_; }
    bool dirty() {
        if (seen_ != scene_.revision()) {
            dirty_ = !saved_ || scene_.document() != *saved_;
            seen_ = scene_.revision();
        }
        return dirty_;
    }
    void open_project(const std::filesystem::path& root, bool allow_empty = false) {
        const auto next_root = std::filesystem::weakly_canonical(root);
        if (!std::filesystem::is_directory(next_root))
            throw std::runtime_error("Project folder does not exist");
        std::shared_ptr<ProjectLease> candidate;
        if (next_root != root_ || !lease_)
            candidate = std::make_shared<ProjectLease>(next_root);
        else
            check_ownership();
        auto name = path_text(next_root.filename());
        auto first = project_file(next_root, "main.scene.json");
        const auto manifest = next_root / "forge.project.json";
        auto settings = std::make_unique<ProjectSettings>(next_root);
        name = settings->document().at("name").get<std::string>();
        const auto startup = settings->startup();
        const bool untitled = std::filesystem::exists(manifest) && !startup;
        if (startup)
            first = project_file(next_root, *startup);
        std::optional<Json> doc;
        if (!untitled && std::filesystem::exists(first))
            doc = read_json(first);
        else if (!untitled && (!allow_empty || std::filesystem::exists(manifest)))
            throw std::runtime_error("Project has no startup scene");
        auto prefabs = std::make_unique<PrefabLibrary>(next_root);
        // Definition scan precedes activation. Malformed assets are reported rather
        // than replacing an already open scene with partial prefab content.
        auto intended = doc ? read_scene_file(first) : empty_scene();
        Scene::validate_document(intended);
        prefabs->load_scene(scene_, intended);
        prefabs_ = std::move(prefabs);
        settings_ = std::move(settings);
        if (candidate)
            lease_ = std::move(candidate);
        ++generation_;
        root_ = next_root;
        name_ = name;
        path_ = untitled ? std::filesystem::path{} : first;
        saved_ = scene_.document();
        disk_ = doc;
        persisted_ = doc.has_value();
        dirty_ = false;
        seen_ = scene_.revision();
        autosaved_revision_ = 0;
    }
    static void create_project(const std::filesystem::path& target, const std::string& name) {
        if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error("Project name must not be blank");
        if (std::filesystem::exists(target))
            throw std::runtime_error(
                "Choose a new project folder; existing folders are not overwritten");
        auto stage = target;
        stage += ".forge-creating";
        if (!std::filesystem::create_directory(stage))
            throw std::runtime_error("Project staging folder already exists");
        try {
            std::filesystem::create_directory(stage / "Scenes");
            std::filesystem::create_directory(stage / "Assets");
            std::filesystem::create_directory(stage / "Native");
            const auto initial = empty_scene();
            atomic_write(stage / "Scenes/main.scene.json", initial.dump(2));
            auto settings = ProjectSettings::defaults(name);
            settings["startup_scene"] = {{"asset", initial.at("asset_id")},
                                         {"source", "Scenes/main.scene.json"}};
            atomic_write(stage / "forge.project.json", settings.dump(2));
            if (std::filesystem::exists(target))
                throw std::runtime_error("Project destination appeared during creation");
            std::filesystem::rename(stage, target);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove_all(stage, ignored);
            throw;
        }
    }
    void open_scene(const std::filesystem::path& path) {
        check_ownership();
        const auto next_path = project_file(root_, path);
        const auto doc = read_json(next_path);
        auto intended = read_scene_file(next_path);
        prefabs().load_scene(scene_, intended);
        ++generation_;
        path_ = next_path;
        saved_ = scene_.document();
        disk_ = doc;
        persisted_ = true;
        dirty_ = false;
        seen_ = scene_.revision();
        autosaved_revision_ = 0;
    }
    void new_scene() {
        check_ownership();
        ++generation_;
        scene_.reset(empty_scene());
        path_.clear();
        saved_.reset();
        disk_.reset();
        persisted_ = false;
        dirty_ = true;
        seen_ = scene_.revision();
        autosaved_revision_ = 0;
    }
    void save_as(const std::filesystem::path& path) {
        check_ownership();
        const auto next_path = project_file(root_, path);
        if (next_path == path_ && saved_) {
            const bool exists = std::filesystem::exists(path_);
            if ((persisted_ && !exists) || (exists && (!disk_ || read_json(path_) != *disk_)))
                throw std::runtime_error(
                    "Scene changed on disk. Use Save As to preserve both versions");
        }
        if (next_path != path_ && std::filesystem::exists(next_path))
            throw std::runtime_error(
                "Save As needs a new filename; an existing scene has its own identity");
        const auto old_recovery = recovery_path();
        const bool copy = persisted_ && next_path != path_;
        const auto output = copy ? duplicate_scene_asset(scene_.document()) : scene_.document();
        write_scene_file(next_path, output);
        if (copy)
            scene_.reset(output); // New logical asset/history after successful disk commit.
        if (path_ != next_path)
            ++generation_;
        path_ = next_path;
        saved_ = scene_.document();
        disk_ = saved_;
        persisted_ = true;
        dirty_ = false;
        seen_ = scene_.revision();
        std::error_code ignored;
        std::filesystem::remove(old_recovery, ignored);
        std::filesystem::remove(recovery_path(), ignored);
    }
    // Adopt an already committed identity-preserving source move without resetting
    // scene state/history or allocating the Save As copy identity.
    void source_relocated(const std::filesystem::path& old_path,
                          const std::filesystem::path& next_path) {
        if (path_.empty() || path_ != ProjectPaths(root_).resolve(old_path))
            return;
        check_ownership();
        const auto next = project_file(root_, next_path);
        const auto old_recovery = recovery_path();
        path_ = next;
        ++generation_;
        // Preserve disk baseline: later external changes still conflict on Save.
        // Keep a dirty draft recoverable under its new locator before dropping the
        // previous snapshot. A recovery write failure leaves the old snapshot intact.
        autosaved_revision_ = 0;
        if (dirty())
            autosave();
        std::error_code ignored;
        std::filesystem::remove(old_recovery, ignored);
    }
    void save() {
        if (path_.empty())
            throw std::runtime_error("Choose a location using Save As");
        save_as(path_);
    }
    std::filesystem::path recovery_path(bool untitled = false) const {
        const auto key = (untitled || path_.empty()) ? std::string("untitled")
                                                     : path_text(path_.lexically_relative(root_));
        std::uint64_t hash = 14695981039346656037ULL;
        for (unsigned char byte : key) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return project_control_file(root_, std::filesystem::path("recovery") /
                                               (std::to_string(hash) + ".json"));
    }
    bool has_recovery() const { return std::filesystem::exists(recovery_path()); }
    bool autosave() {
        check_ownership();
        if (!dirty() || autosaved_revision_ == scene_.revision())
            return false;
        atomic_write(
            recovery_path(),
            Json{{"version", 1},
                 {"scene", path_.empty() ? "" : path_text(path_.lexically_relative(root_))},
                 {"base", saved_ ? *saved_ : Json{}},
                 {"document", scene_.document()}}
                .dump(2));
        autosaved_revision_ = scene_.revision();
        return true;
    }
    void recover() {
        check_ownership();
        const auto data = read_json(recovery_path());
        const auto expected = path_.empty() ? "" : path_text(path_.lexically_relative(root_));
        if (data.at("version") != 1 || data.at("scene") != expected ||
            (data.at("base") != (saved_ ? *saved_ : Json{}) &&
             data.at("base") != (disk_ ? *disk_ : Json{})))
            throw std::runtime_error(
                "Recovery does not match the current disk scene; recovery file preserved");
        scene_.edit(reconcile_prefab_intent(data.at("document"), scene_.prefab_sources()));
        seen_ = 0;
    }
    void recover_untitled() {
        check_ownership();
        const auto data = read_json(recovery_path(true));
        if (data.at("version") != 1 || data.at("scene") != "" || !data.at("base").is_null())
            throw std::runtime_error("Invalid untitled recovery record");
        scene_.reset(reconcile_prefab_intent(data.at("document"), scene_.prefab_sources()));
        ++generation_;
        path_.clear();
        saved_.reset();
        disk_.reset();
        persisted_ = false;
        dirty_ = true;
        seen_ = scene_.revision();
        autosaved_revision_ = 0;
    }
    bool has_untitled_recovery() const { return std::filesystem::exists(recovery_path(true)); }
    void discard_recovery(bool untitled = false) {
        check_ownership();
        std::filesystem::remove(recovery_path(untitled));
    }

  private:
    Scene& scene_;
    std::unique_ptr<ProjectSettings> settings_;
    std::unique_ptr<PrefabLibrary> prefabs_;
    std::shared_ptr<ProjectLease> lease_;
    std::uint64_t generation_ = 0;
    std::filesystem::path root_, path_;
    std::string name_;
    std::optional<Json> saved_, disk_;
    std::uint64_t seen_ = 0, autosaved_revision_ = 0;
    bool dirty_ = false, persisted_ = false;
};
} // namespace forge
