#include "standalone_manifest.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "native_io_path.hpp"
#include <forge/game_content.hpp>
#include <forge/project.hpp>
#include <set>
namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("game.distribution: " + message);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("game.export.cancelled: Distribution validation cancelled");
}
} // namespace
Json verify_distribution_files(const std::filesystem::path& root,
                               const std::filesystem::path& manifest, std::string_view format,
                               std::stop_token stop) {
    const ProjectPaths paths(root);
    const auto name = ProjectPaths::normalize(manifest);
    const auto bytes = asset_detail::read_bytes(paths.resolve(name), 16 * 1024 * 1024);
    auto result = asset_detail::parse_bounded_json(bytes, 16 * 1024 * 1024, 1000000, 32);
    require(result.at("format") == format && result.at("version") == 1,
            "Unsupported manifest format/version");
    const auto& files = result.at("files");
    require(files.is_object() && !files.empty() && files.size() <= 65536, "Invalid file inventory");
    std::set<std::filesystem::path, ProjectLocatorLess> expected;
    std::uint64_t total = bytes.size();
    for (const auto& [key, info] : files.items()) {
        cancelled(stop);
        const auto locator = ProjectPaths::normalize(std::filesystem::u8path(key));
        require(path_utf8(locator) == key && locator != name && expected.insert(locator).second,
                "Duplicate or noncanonical distribution path");
        require(info.at("bytes").is_number_unsigned(), "Invalid file size");
        const auto size = info.at("bytes").get<std::uint64_t>();
        require(size <= 512ull * 1024 * 1024 && size <= 4ull * 1024 * 1024 * 1024 - total,
                "Distribution exceeds file/aggregate byte budget");
        total += size;
        const auto data = asset_detail::read_bytes(paths.resolve(locator), std::size_t(size));
        require(data.size() == size && info.at("sha256") == asset_detail::content_digest(data),
                "File hash/size mismatch: " + key);
    }
    std::size_t entries = 0;
    std::set<std::filesystem::path, ProjectLocatorLess> found;
    for (const auto& item : std::filesystem::recursive_directory_iterator(
             asset_detail::native_io_path(paths.root()))) {
        cancelled(stop);
        require(++entries <= 131072, "Distribution directory budget exceeded");
        const auto status = item.symlink_status();
        require(!std::filesystem::is_symlink(status), "Distribution contains a link");
        if (std::filesystem::is_directory(status))
            continue;
        require(std::filesystem::is_regular_file(status), "Distribution contains a special file");
        const auto locator =
            item.path().lexically_relative(asset_detail::native_io_path(paths.root()));
        if (locator != name)
            found.insert(locator);
    }
    require(found == expected, "Distribution file inventory differs from manifest");
    return result;
}
StandaloneDistribution open_standalone_distribution(const std::filesystem::path& root,
                                                    const RuntimePackageTarget& target,
                                                    std::string_view profile,
                                                    std::string_view fingerprint,
                                                    std::stop_token stop) {
    auto manifest =
        verify_distribution_files(root, "forge.standalone.json", "forge.standalone", stop);
    require(manifest.at("target").at("platform") == target.platform &&
                manifest.at("target").at("backend") == target.backend,
            "Runtime platform/backend does not match this distribution");
    const auto& engine = manifest.at("engine");
    require(engine.at("profile") == profile && engine.at("sdk_fingerprint") == fingerprint,
            "Runtime engine/SDK fingerprint mismatch");
    require(engine.at("source_commit").is_string() && engine.at("build_id").is_string(),
            "Missing engine build provenance");
    const ProjectPaths paths(root);
    const auto content_locator =
        ProjectPaths::normalize(std::filesystem::u8path(manifest.at("content").get<std::string>()));
    require(content_locator == "content", "Unsupported content root");
    auto settings = manifest.at("settings");
    ProjectSettings::validate(settings);
    require(settings.contains("game") && !settings.at("startup_scene").is_null(),
            "Game defaults and startup scene are required");
    const auto content = paths.resolve(content_locator);
    const auto catalog = open_runtime_content(content, target, {}, stop);
    const auto startup = settings.at("startup_scene").at("asset").get<AssetId>();
    (void)load_game_scene(content, {startup});
    require(manifest.at("assets") == runtime_asset_inventory(catalog),
            "Distribution closure differs from the admitted catalog");
    for (const auto& module : settings.value("modules", Json::array())) {
        if (module.is_string())
            continue; // Recognized linked engine module; ProjectSettings validated it.
        require(module.is_object() && profile == "shared-native-sdk" &&
                    module.at("fingerprint") == fingerprint,
                "Standalone native modules require the exact shared SDK");
        const auto file = path_utf8(ProjectPaths::normalize(
            std::filesystem::u8path(module.at("library").get<std::string>())));
        require(file.starts_with("native/") && manifest.at("files").contains(file),
                "Native module is absent from the distribution");
    }
    require(manifest.at("files").contains("resources/ui/LatoLatin-Regular.ttf"),
            "Default UI font is missing");
    return {paths.root(), content, std::move(manifest), std::move(settings)};
}
} // namespace forge
