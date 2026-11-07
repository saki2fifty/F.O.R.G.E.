#include "standalone_manifest.hpp"
#include "animation_clip_selection.hpp"
#include "asset_bytes.hpp"
#include "audio_stream_profile.hpp"
#include "bounded_json.hpp"
#include "fbx_profile.hpp"
#include "native_io_path.hpp"
#include "runtime_execution_values.hpp"
#include "runtime_reflection_profile.hpp"
#include "ui_filter_profile.hpp"
#include "ui_gradient_profile.hpp"
#include "ui_surface_limits.hpp"
#include <algorithm>
#include <forge/animation_components.hpp>
#include <forge/animation_graph.hpp>
#include <forge/audio_components.hpp>
#include <forge/fog_volume.hpp>
#include <forge/game_content.hpp>
#include <forge/input_message.hpp>
#include <forge/mesh_wind.hpp>
#include <forge/model_asset.hpp>
#include <forge/project.hpp>
#include <forge/shader_asset.hpp>
#include <forge/ui_protocol.hpp>
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
    require(result.at("format").get<std::string>() == format && result.at("version") == 1,
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
void validate_runtime_compatibility(const AssetCatalog& catalog, const Json& engine,
                                    const Json* project) {
    if (project && detail::read_runtime_execution(*project).ecs_workers > 1) {
        const auto support = engine.find("runtime_ecs_workers");
        require(support != engine.end() && support->is_number_integer() &&
                    *support == runtime_ecs_worker_profile_version,
                "Fixed ECS workers require a matching runtime kit; select the kit packaged "
                "with this editor");
    }
    for (const auto& [id, record] : catalog.records()) {
        (void)id;
        if (record.type == ModelAsset::type) {
            const auto imported = record.metadata.find("forge.import");
            if (imported != record.metadata.end() &&
                imported->value("importer", std::string{}) == asset_detail::fbx_importer_id) {
                const auto support = engine.find("runtime_model_fbx");
                require(support != engine.end() && support->is_number_integer() &&
                            *support == model_fbx_runtime_profile_version,
                        "FBX Models require a matching runtime kit; select the kit packaged "
                        "with this editor");
            }
        }
        if (record.type == AnimationGraphAsset::type) {
            const auto support = engine.find("runtime_animation_graph");
            require(support != engine.end() && support->is_number_integer() &&
                        *support == animation_graph_runtime_profile_version,
                    "Animation Graphs require a matching runtime kit; select the kit packaged with "
                    "this editor");
        }
        if (record.type == AnimationClipAsset::type &&
            record.metadata.contains("forge.animation_clip")) {
            const auto support = engine.find("runtime_skeletal_clip");
            require(support != engine.end() && support->is_number_integer() &&
                        *support == animation_detail::baked_clip_profile_version,
                    "Baked skeletal clips require a matching runtime kit; select the kit "
                    "packaged with this editor");
        }
        if (record.type == AudioClipAsset::type) {
            const auto metadata = record.metadata.find("forge.audio");
            if (metadata != record.metadata.end() &&
                metadata->value("playback_storage", std::string("resident")) == "stream") {
                const auto support = engine.find("runtime_audio_streaming");
                require(support != engine.end() && support->is_number_integer() &&
                            *support == asset_detail::audio_streaming_profile_version,
                        "Streamed Audio Clips require a matching runtime kit; select the kit "
                        "packaged with this editor");
            }
        }
        if (record.type == ShaderAsset::type) {
            const auto& metadata = record.metadata.at("forge.shader");
            if (const auto wrapper = metadata.find("surface_wrapper_version");
                wrapper != metadata.end()) {
                require(wrapper->is_number_integer() && *wrapper >= 1 &&
                            *wrapper <= current_surface_wrapper_version,
                        "Invalid Shader runtime surface profile");
                if (*wrapper >= first_explicit_surface_runtime_profile) {
                    const auto support = engine.find("runtime_surface_wrapper");
                    require(support != engine.end() && support->is_number_integer() &&
                                *support >= *wrapper,
                            "Shader surface requires a matching runtime kit; select the kit "
                            "packaged with this editor");
                }
            }
        }
        if (const auto document = record.metadata.find("forge.runtime_document");
            document != record.metadata.end()) {
            const auto features = document->find("runtime_features");
            if (features != document->end()) {
                require(features->is_object(), "Invalid authored document runtime features");
                for (const auto& [name, version] : features->items()) {
                    if (name == "sequence_rate") {
                        require(version.is_number_integer() &&
                                    version == sequence_rate_runtime_profile_version,
                                "Unsupported authored sequence placement-rate profile");
                        const auto support = engine.find("runtime_sequence_rate");
                        require(support != engine.end() && support->is_number_integer() &&
                                    *support == sequence_rate_runtime_profile_version,
                                "Sequence placement rates require the runtime kit packaged with "
                                "this editor");
                        continue;
                    }
                    if (name == "skeletal_sequence") {
                        require(version.is_number_integer() &&
                                    version == skeletal_sequence_runtime_profile_version,
                                "Unsupported authored skeletal sequence profile");
                        const auto support = engine.find("runtime_skeletal_sequence");
                        require(support != engine.end() && support->is_number_integer() &&
                                    *support == skeletal_sequence_runtime_profile_version,
                                "Skeletal sequence tracks require a matching runtime kit; select "
                                "the runtime kit packaged with this editor");
                        continue;
                    }
                    if (name == "reflection_capture") {
                        require(version.is_number_integer() &&
                                    version == runtime_reflection_profile_version,
                                "Unsupported authored document runtime reflection profile");
                        const auto support = engine.find("runtime_reflection_capture");
                        require(
                            support != engine.end() && support->is_number_integer() &&
                                *support == runtime_reflection_profile_version,
                            "Runtime reflection capture requires a matching runtime kit; select "
                            "the runtime kit packaged with this editor");
                        continue;
                    }
                    if (name == "ui_surface_input") {
                        require(version.is_number_integer() &&
                                    version >= ui_surface_detail::quad_profile_version &&
                                    version <= ui_surface_detail::profile_version,
                                "Unsupported authored document runtime feature/profile");
                        const auto support = engine.find("runtime_ui_surface_input");
                        require(support != engine.end() && support->is_number_integer() &&
                                    *support >= version &&
                                    *support <= ui_surface_detail::profile_version,
                                "UI Surface Input requires a matching runtime kit; select the "
                                "runtime kit packaged with this editor");
                        continue;
                    }
                    if (name == "fog_volumes") {
                        require(version.is_number_integer() &&
                                    version == fog_volume_profile_version,
                                "Unsupported authored document runtime feature/profile");
                        const auto support = engine.find("runtime_fog_volumes");
                        require(support != engine.end() && support->is_number_integer() &&
                                    *support == fog_volume_profile_version,
                                "Fog Volumes require a matching runtime kit; select the runtime "
                                "kit packaged with this editor");
                        continue;
                    }
                    if (name == "input_message_triggers") {
                        require(version.is_number_integer() &&
                                    version == input_message_trigger_profile_version,
                                "Unsupported authored document runtime feature/profile");
                        const auto support = engine.find("runtime_input_message_triggers");
                        require(support != engine.end() && support->is_number_integer() &&
                                    *support == input_message_trigger_profile_version,
                                "Input Message triggers require a matching runtime kit; select "
                                "the runtime kit packaged with this editor");
                        continue;
                    }
                    require(name == "mesh_wind" && version.is_number_integer() &&
                                version == mesh_wind_profile_version,
                            "Unsupported authored document runtime feature/profile");
                    const auto support = engine.find("runtime_mesh_wind");
                    require(support != engine.end() && support->is_number_integer() &&
                                *support == mesh_wind_profile_version,
                            "Mesh wind requires a matching runtime kit with wind geometry support; "
                            "select the runtime kit packaged with this editor");
                }
            }
        }
        for (const auto& edge : record.dependency_edges)
            if (edge.kind == AssetDependencyKind::Runtime &&
                edge.role == "document:forge.ui_document.render_texture") {
                const auto protocol = engine.find("runtime_ui_protocol");
                require(protocol != engine.end() && protocol->is_number_integer() &&
                            *protocol == ui_protocol::version,
                        "Camera-output UI requires a matching runtime kit with UI image-output "
                        "support; select the runtime kit packaged with this editor");
            }
        const auto source = record.metadata.find("forge.ui_source");
        if (source == record.metadata.end())
            continue;
        const auto features = source->find("runtime_features");
        if (features == source->end())
            continue;
        require(features->is_object(), "Invalid UI source runtime features");
        for (const auto& [name, version] : features->items()) {
            if (name == "gradients") {
                require(version.is_number_integer() &&
                            version >= ui_gradient_detail::basic_profile_version &&
                            version <= ui_gradient_detail::profile_version,
                        "Unsupported UI gradient runtime profile");
                const auto support = engine.find("runtime_ui_gradients");
                require(support != engine.end() && support->is_number_integer() &&
                            *support >= version && *support <= ui_gradient_detail::profile_version,
                        "UI gradients require a matching runtime kit with UI gradient support; "
                        "select the runtime kit packaged with this editor");
                continue;
            }
            require(name == "filters" && version.is_number_integer() &&
                        version >= ui_filter_detail::basic_profile_version &&
                        version <= ui_filter_detail::profile_version,
                    "Unsupported UI source runtime feature/profile");
            const auto support = engine.find("runtime_ui_filters");
            require(support != engine.end() && support->is_number_integer() &&
                        *support >= version && *support <= ui_filter_detail::profile_version,
                    "UI filters require a matching runtime kit with UI filter support; "
                    "select the runtime kit packaged with this editor");
        }
    }
}
StandaloneDistribution open_standalone_distribution(const std::filesystem::path& root,
                                                    const RuntimePackageTarget& target,
                                                    std::string_view profile,
                                                    std::string_view fingerprint,
                                                    std::stop_token stop) {
    auto manifest =
        verify_distribution_files(root, "forge.standalone.json", "forge.standalone", stop);
    require(manifest.at("profile") == "development", "Unsupported standalone runtime profile");
    const auto executable = ProjectPaths::normalize(
        std::filesystem::u8path(manifest.at("executable").get<std::string>()));
    require(executable.parent_path().empty() &&
                manifest.at("files").contains(path_utf8(executable)),
            "Standalone executable is missing from its inventory");
    require(manifest.at("target").at("platform") == target.platform &&
                manifest.at("target").at("backend") == target.backend,
            "Runtime platform/backend does not match this distribution");
    const auto& engine = manifest.at("engine");
    require(engine.at("profile").get<std::string>() == profile &&
                engine.at("sdk_fingerprint").get<std::string>() == fingerprint,
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
    require(settings.at("game").at("profile") == manifest.at("profile"),
            "Standalone and game configuration profiles differ");
    const auto content = paths.resolve(content_locator);
    const auto catalog = open_runtime_content(content, target, {}, stop);
    validate_runtime_compatibility(catalog, engine, &settings);
    const auto startup = settings.at("startup_scene").at("asset").get<AssetId>();
    (void)load_game_scene(content, {startup});
    require(manifest.at("assets") == runtime_asset_inventory(catalog),
            "Distribution closure differs from the admitted catalog");
    const auto deployments = manifest.value("native_modules", Json::array());
    require(deployments.is_array() && deployments.size() <= 64,
            "Invalid native module provenance inventory");
    std::set<std::string> native_ids;
    for (const auto& module : settings.value("modules", Json::array())) {
        if (module.is_string())
            continue; // Recognized linked engine module; ProjectSettings validated it.
        require(module.is_object() && profile == "shared-native-sdk" &&
                    module.at("fingerprint").get<std::string>() == fingerprint,
                "Standalone native modules require the exact shared SDK");
        const auto file = path_utf8(ProjectPaths::normalize(
            std::filesystem::u8path(module.at("library").get<std::string>())));
        require(file.starts_with("native/") && manifest.at("files").contains(file),
                "Native module is absent from the distribution");
        const auto id = module.at("id").get<std::string>();
        require(native_ids.insert(id).second, "Duplicate native module identity");
        const auto found =
            std::find_if(deployments.begin(), deployments.end(), [&](const auto& entry) {
                return entry.at("id").template get<std::string>() == id;
            });
        require(found != deployments.end(), "Native module provenance missing: " + id);
        const auto& deployment = found->at("deployment");
        require(deployment.at("format") == "forge.module-kit" && deployment.at("version") == 1 &&
                    deployment.at("fingerprint").get<std::string>() == fingerprint &&
                    deployment.at("target") == manifest.at("target"),
                "Native module provenance is incompatible: " + id);
        const auto library = ProjectPaths::normalize(
            std::filesystem::u8path(deployment.at("library").template get<std::string>()));
        require(library.parent_path().empty() &&
                    file == path_utf8(std::filesystem::path("native") / id / library) &&
                    deployment.at("files").is_object() &&
                    deployment.at("files").contains(path_utf8(library)),
                "Native module library and deployment disagree: " + id);
        for (const auto& [name, info] : deployment.at("files").items()) {
            const auto locator = ProjectPaths::normalize(std::filesystem::u8path(name));
            require(locator.parent_path().empty(), "Module provenance contains a non-native path");
            const auto destination = locator == library ? file : path_utf8(locator);
            require(manifest.at("files").contains(destination) &&
                        manifest.at("files").at(destination) == info,
                    "Native module dependency differs from deployed bytes: " + destination);
        }
    }
    require(native_ids.size() == deployments.size(),
            "Orphan or duplicate native module provenance");
    require(manifest.at("files").contains("resources/ui/LatoLatin-Regular.ttf"),
            "Default UI font is missing");
    return {paths.root(), content, std::move(manifest), std::move(settings)};
}
} // namespace forge
