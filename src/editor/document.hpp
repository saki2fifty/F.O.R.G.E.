#pragma once
#include <algorithm>
#include <cctype>
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
    const auto resolved =
        std::filesystem::weakly_canonical(path.is_absolute() ? path : root / path);
    const auto relative = resolved.lexically_relative(std::filesystem::weakly_canonical(root));
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
        if (std::filesystem::exists(manifest)) {
            const auto data = read_json(manifest);
            if (data.at("version") != 1)
                throw std::runtime_error("Unsupported project version");
            name = data.at("name").get<std::string>();
            first = project_file(
                next_root, std::filesystem::u8path(data.at("startup_scene").get<std::string>()));
        }
        std::optional<Json> doc;
        if (std::filesystem::exists(first))
            doc = read_json(first);
        else if (!allow_empty || std::filesystem::exists(manifest))
            throw std::runtime_error("Project has no startup scene");
        scene_.reset(doc.value_or(Json{{"version", 1}, {"entities", Json::array()}}));
        if (candidate)
            lease_ = std::move(candidate);
        ++generation_;
        root_ = next_root;
        name_ = name;
        path_ = first;
        saved_ = doc.value_or(scene_.document());
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
            atomic_write(stage / "Scenes/main.scene.json",
                         Json{{"version", 1}, {"entities", Json::array()}}.dump(2));
            atomic_write(
                stage / "forge.project.json",
                Json{{"version", 1}, {"name", name}, {"startup_scene", "Scenes/main.scene.json"}}
                    .dump(2));
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
        scene_.reset(doc);
        ++generation_;
        path_ = next_path;
        saved_ = scene_.document();
        persisted_ = true;
        dirty_ = false;
        seen_ = scene_.revision();
        autosaved_revision_ = 0;
    }
    void new_scene() {
        check_ownership();
        ++generation_;
        scene_.reset(Json{{"version", 1}, {"entities", Json::array()}});
        path_.clear();
        saved_.reset();
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
            if ((persisted_ && !exists) || (exists && read_json(path_) != *saved_))
                throw std::runtime_error(
                    "Scene changed on disk. Use Save As to preserve both versions");
        }
        const auto old_recovery = recovery_path();
        scene_.save(next_path);
        if (path_ != next_path)
            ++generation_;
        path_ = next_path;
        saved_ = scene_.document();
        persisted_ = true;
        dirty_ = false;
        seen_ = scene_.revision();
        std::error_code ignored;
        std::filesystem::remove(old_recovery, ignored);
        std::filesystem::remove(recovery_path(), ignored);
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
            data.at("base") != (saved_ ? *saved_ : Json{}))
            throw std::runtime_error(
                "Recovery does not match the current disk scene; recovery file preserved");
        scene_.edit(data.at("document"));
        seen_ = 0;
    }
    void recover_untitled() {
        check_ownership();
        const auto data = read_json(recovery_path(true));
        if (data.at("version") != 1 || data.at("scene") != "" || !data.at("base").is_null())
            throw std::runtime_error("Invalid untitled recovery record");
        scene_.reset(data.at("document"));
        ++generation_;
        path_.clear();
        saved_.reset();
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
    std::shared_ptr<ProjectLease> lease_;
    std::uint64_t generation_ = 0;
    std::filesystem::path root_, path_;
    std::string name_;
    std::optional<Json> saved_;
    std::uint64_t seen_ = 0, autosaved_revision_ = 0;
    bool dirty_ = false, persisted_ = false;
};
} // namespace forge
