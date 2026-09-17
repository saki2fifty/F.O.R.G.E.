#include <cmath>
#include <forge/native_sdk.hpp>
#include <forge/project.hpp>
#include <forge/scene.hpp>
#include <fstream>
namespace forge {
namespace {
Json read(const std::filesystem::path& p, std::uintmax_t limit = 8 * 1024 * 1024) {
    if (std::filesystem::file_size(p) > limit)
        throw std::runtime_error("Project source exceeds size limit: " + path_utf8(p));
    std::ifstream f(p);
    if (!f)
        throw std::runtime_error("Cannot read project file: " + path_utf8(p));
    return Json::parse(f);
}
} // namespace
Json ProjectSettings::defaults(const std::string& name) {
    return {{"version", 2},
            {"name", name},
            {"simulation_hz", 60},
            {"physics", {{"version", 1}, {"gravity", {0, -9.81, 0}}}},
            {"startup_scene", nullptr},
            {"input", InputMap{}.source()}};
}
void ProjectSettings::validate(const Json& data) {
    if (!data.is_object() || !data.at("version").is_number_integer() || data.at("version") != 2 ||
        !data.at("name").is_string() || data.at("name").get<std::string>().empty())
        throw std::runtime_error("Expected project version 2 and a name");
    const double hz = data.value("simulation_hz", 60.0);
    if (!std::isfinite(hz) || hz < 1 || hz > 240)
        throw std::runtime_error("Project simulation frequency must be 1..240 Hz");
    (void)InputMap(data.at("input"));
    if (!data.at("startup_scene").is_null()) {
        const auto& startup = data.at("startup_scene");
        (void)startup.at("asset").get<AssetId>();
        (void)ProjectPaths::normalize(
            std::filesystem::u8path(startup.at("source").get<std::string>()));
    }
    if (data.contains("physics")) {
        const auto& p = data.at("physics");
        if (!p.is_object() || p.at("version") != 1 || !p.at("gravity").is_array() ||
            p.at("gravity").size() != 3)
            throw std::runtime_error("Expected physics settings version 1 and three gravity axes");
        PhysicsConfig config{p.at("gravity").get<std::array<double, 3>>()};
        config.validate();
    }
    validate_project_modules(data);
}
ProjectSettings::ProjectSettings(std::filesystem::path root)
    : paths_(std::move(root)), data_(defaults(path_utf8(paths_.root().filename()))) {
    const auto manifest = paths_.resolve("forge.project.json");
    if (!std::filesystem::exists(manifest))
        return;
    disk_ = read(manifest);
    data_ = *disk_;
    if (!data_.at("version").is_number_integer() ||
        (data_.at("version") != 1 && data_.at("version") != 2))
        throw std::runtime_error("Unsupported project version");
    if (data_.at("version") == 1) {
        const auto locator = ProjectPaths::normalize(
            std::filesystem::u8path(data_.at("startup_scene").get<std::string>()));
        const auto scene = read_scene_file(paths_.resolve(locator));
        data_["version"] = 2;
        data_["startup_scene"] = {{"asset", scene.at("asset_id")}, {"source", path_utf8(locator)}};
        if (!data_.contains("simulation_hz"))
            data_["simulation_hz"] = 60;
        if (!data_.contains("input"))
            data_["input"] = InputMap{}.source();
    }
    validate(data_);
}
std::optional<std::filesystem::path> ProjectSettings::startup() const {
    if (data_.at("startup_scene").is_null())
        return {};
    const auto& ref = data_.at("startup_scene");
    const auto id = ref.at("asset").get<AssetId>();
    std::optional<std::filesystem::path> match;
    // Identity is authority. Scan also detects copied files with duplicate IDs.
    std::size_t examined = 0;
    for (std::filesystem::recursive_directory_iterator it(paths_.root()), end; it != end; ++it) {
        if (++examined > 20000)
            throw std::runtime_error("Startup scene scan limit exceeded");
        if (it->is_symlink()) {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory()) {
            if (it->path().filename() == ".forge" || it->path().filename() == ".git" ||
                it.depth() >= 16)
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file() || it->path().extension() != ".json" ||
            it->file_size() > 16 * 1024 * 1024 || it->path().filename() == "forge.project.json")
            continue;
        try {
            const auto doc = read(it->path(), 16 * 1024 * 1024);
            if (!doc.contains("entities") || doc.value("asset_id", Json()) != Json(id))
                continue;
            Scene::validate_document(doc);
        } catch (const std::exception&) {
            continue;
        }
        if (match)
            throw std::runtime_error("Startup scene AssetId is ambiguous (copied asset)");
        match = it->path();
    }
    // Legacy source uses its durable identity migration journal without rewriting source.
    if (!match) {
        const auto hint =
            paths_.resolve(std::filesystem::u8path(ref.at("source").get<std::string>()));
        if (std::filesystem::exists(hint) &&
            read_scene_file(hint).at("asset_id").get<AssetId>() == id)
            match = hint;
    }
    if (!match)
        throw std::runtime_error("Startup scene AssetId is missing: " + id.str());
    return match;
}
void ProjectSettings::save(Json candidate, const Json* expected) {
    if (expected && data_ != *expected)
        throw std::runtime_error(
            "Settings changed since this draft opened; discard edits and retry");
    validate(candidate);
    if (candidate.dump().size() > 8 * 1024 * 1024)
        throw std::runtime_error("Project settings exceed 8 MiB");
    auto prepared = *this;
    prepared.data_ = candidate;
    (void)prepared.startup(); // Validate identity/type resolution before persistence.
    const auto manifest = paths_.resolve("forge.project.json");
    const auto current =
        std::filesystem::exists(manifest) ? std::optional<Json>(read(manifest)) : std::nullopt;
    if (current != disk_)
        throw std::runtime_error("Project settings changed on disk; reopen before saving");
    if (disk_ && disk_->at("version") == 1) {
        const auto backup = paths_.resolve("forge.project.json.v1.backup");
        if (std::filesystem::exists(backup)) {
            if (read(backup) != *disk_)
                throw std::runtime_error("Project migration backup conflict");
        } else {
            std::string original;
            {
                std::ifstream stream(manifest, std::ios::binary);
                original.assign(std::istreambuf_iterator<char>(stream), {});
            }
            if (Json::parse(original) != *disk_)
                throw std::runtime_error("Project changed while preparing migration backup");
            atomic_write(backup, original);
        }
    }
    // Prepare in-memory copies before the durable single-file replacement.
    auto saved = candidate;
    atomic_write(manifest, candidate.dump(2));
    data_.swap(candidate);
    disk_ = std::move(saved);
}
} // namespace forge
