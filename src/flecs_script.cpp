#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "asset_worker.hpp"
#include <forge/flecs_script.hpp>
#include <forge/project_paths.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <memory>
#include <set>
namespace forge {
namespace {
constexpr std::size_t text_limit = 1024 * 1024;
std::unique_ptr<ProjectPaths> script_paths;
std::string script_locator, script_code;
std::set<std::string> included;
std::size_t read_bytes = 0;
std::string file_error;
FILE* confined_open(const char* filename, const char* mode) noexcept {
    try {
        if (!script_paths || !filename || !mode ||
            (std::string(mode) != "r" && std::string(mode) != "rb"))
            throw std::runtime_error("Flecs scripts can only read project-contained source files");
        const auto relative = ProjectPaths::normalize(std::filesystem::u8path(filename));
        (void)script_paths->resolve(relative);
        if (relative.extension() != ".flecs")
            throw std::runtime_error("Script includes must be .flecs files");
        const auto key = path_utf8(relative);
        if (included.insert(key).second && included.size() > 128)
            throw std::runtime_error("Script exceeds 128 included files");
        std::string code;
        if (key == script_locator)
            code = script_code;
        else
            code = read_flecs_script_source(script_paths->root(), relative);
        read_bytes += code.size();
        if (read_bytes > 8 * text_limit || code.find('\0') != std::string::npos)
            throw std::runtime_error("Script input exceeds bounds or contains a NUL byte");
        // Native parser expects FILE*. The anonymous temporary is closed by its
        // normal fclose hook; no machine path is exposed to the language.
        FILE* stream = std::tmpfile();
        if (!stream)
            throw std::runtime_error("Cannot allocate script input stream");
        if (std::fwrite(code.data(), 1, code.size(), stream) != code.size()) {
            std::fclose(stream);
            throw std::runtime_error("Cannot prepare script input stream");
        }
        std::rewind(stream);
        return stream;
    } catch (const std::exception& e) {
        file_error =
            std::string("Script file ") + (filename ? filename : "<null>") + ": " + e.what();
        return nullptr;
    }
}
Json script_error(const std::string& message, int line = 0, int column = 0) {
    return {{"ok", false},
            {"source", script_locator},
            {"error", message},
            {"line", line},
            {"column", column}};
}
class ScriptLogCapture {
  public:
    ScriptLogCapture() { ecs_log_start_capture(true); }
    ~ScriptLogCapture() {
        if (active_)
            ecs_os_free(ecs_log_stop_capture());
    }
    std::string finish() {
        active_ = false;
        char* text = ecs_log_stop_capture();
        std::unique_ptr<char, decltype(ecs_os_api.free_)> owned(text, ecs_os_api.free_);
        return text ? text : "";
    }

  private:
    bool active_ = true;
};
} // namespace
std::string read_flecs_script_source(const std::filesystem::path& project,
                                     const std::filesystem::path& source) {
    ProjectPaths paths(project);
    const auto locator = ProjectPaths::normalize(source);
    const auto file = paths.resolve(locator);
    if (locator.extension() != ".flecs" || !std::filesystem::is_regular_file(file))
        throw std::runtime_error("Choose a project-contained regular .flecs file");
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open script source");
    // Bound the read itself: checking file_size alone races a changing file.
    std::string code(text_limit + 1, '\0');
    input.read(code.data(), std::streamsize(code.size()));
    const auto count = std::size_t(input.gcount());
    if (input.bad() || count > text_limit)
        throw std::runtime_error("Cannot read script or source exceeds 1 MiB");
    code.resize(count);
    if (code.find('\0') != std::string::npos)
        throw std::runtime_error("Script source contains a NUL byte");
    return code;
}
bool publish_flecs_script_preview(const Json& candidate, const std::string& source,
                                  std::string& published_source, std::string& published_result) {
    if (!candidate.is_object() || !candidate.contains("ok") || !candidate.at("ok").is_boolean())
        throw std::runtime_error("Invalid Script candidate result");
    if (!candidate.at("ok").get<bool>())
        return false;
    if (!candidate.contains("entities") || !candidate.at("entities").is_array())
        throw std::runtime_error("Script candidate has no generated entity snapshot");
    auto next_source = source;
    auto next_result = candidate.dump(2);
    published_source.swap(next_source);
    published_result.swap(next_result);
    return true;
}
AssetRecord register_flecs_script(const std::filesystem::path& project,
                                  const std::filesystem::path& source) {
    ProjectPaths paths(project);
    const auto locator = ProjectPaths::normalize(source);
    const auto file = paths.resolve(locator);
    if (locator.extension() != ".flecs" || !std::filesystem::is_regular_file(file) ||
        std::filesystem::file_size(file) > text_limit)
        throw std::runtime_error("Choose a project-contained .flecs file of at most 1 MiB");
    const auto digest = asset_detail::content_digest(asset_detail::read_bytes(file, text_limit));
    const auto index = AssetCatalog::project_index(project);
    const auto baseline = asset_storage::read(index, max_asset_index_bytes);
    auto catalog = AssetCatalog::open_project(project);
    AssetRecord record{AssetId::generate(), FlecsScriptAsset::type, locator};
    bool existed = false;
    for (const auto& [id, previous] : catalog.records()) {
        (void)id;
        if (paths.same_locator(previous.source, locator)) {
            if (previous.type != FlecsScriptAsset::type)
                throw std::runtime_error("Source is registered as another asset type");
            record = previous;
            existed = true;
        }
    }
    record.metadata["flecs_revision"] = "fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8";
    record.metadata["language"] = "Flecs Script";
    std::erase_if(record.source_dependencies,
                  [](const auto& edge) { return edge.role == "script.registered"; });
    record.source_dependencies.push_back({locator, "script.registered", digest});
    if (existed)
        catalog.replace(record);
    else
        catalog.add(record);
    if (asset_storage::read(index, max_asset_index_bytes) != baseline ||
        asset_detail::content_digest(asset_detail::read_bytes(file, text_limit)) != digest)
        throw std::runtime_error("Script source/catalog changed during registration; retry");
    catalog.save(index);
    return record;
}
Json evaluate_flecs_script_worker(const Json& request) {
    script_paths = std::make_unique<ProjectPaths>(
        std::filesystem::u8path(request.at("project").get<std::string>()));
    script_locator = path_utf8(
        ProjectPaths::normalize(std::filesystem::u8path(request.at("source").get<std::string>())));
    (void)script_paths->resolve(std::filesystem::u8path(script_locator));
    script_code = request.at("code").get<std::string>();
    const auto previous = request.value("previous", std::string{});
    if (script_code.size() > text_limit || previous.size() > text_limit ||
        script_code.find('\0') != std::string::npos || previous.find('\0') != std::string::npos)
        return script_error("Script exceeds 1 MiB or contains a NUL byte");
    // The code-based root constructor bypasses fopen. Include those supplied
    // bytes and the root file in the same total-input/count budget explicitly.
    included = {script_locator};
    read_bytes = script_code.size() + previous.size();
    file_error.clear();
    ecs_os_set_api_defaults();
    auto api = ecs_os_get_api();
    api.fopen_ = confined_open;
    ecs_os_set_api(&api);
    EngineContext preview(WorldRole::Preview);
    auto& world = preview.world().world();
    ECS_IMPORT(world, FlecsScriptMath);
    // Parse separately for stable source coordinates; parsing does not evaluate
    // functions or mutate generated content.
    ecs_script_eval_result_t error{};
    auto* parsed =
        ecs_script_parse(world, script_locator.c_str(), script_code.c_str(), nullptr, &error);
    if (!parsed) {
        const auto result = script_error(error.error ? error.error : "Script parse failed",
                                         error.line, error.column);
        ecs_os_free(error.error);
        return result;
    }
    ecs_script_free(parsed);
    ecs_script_desc_t desc{};
    const auto candidate = script_code;
    if (!previous.empty())
        script_code = previous;
    // Keep source-buffer ownership in FORGE. In pinned 4.1.6 the filename
    // constructor leaks its loaded buffer when managed evaluation fails.
    // The native EcsScript component owns its filename for relative includes.
    desc.entity = ecs_new_from_path_w_sep(world, 0, script_locator.c_str(), "/", nullptr);
    ecs_ensure(world, desc.entity, EcsScript)->filename = ecs_os_strdup(script_locator.c_str());
    desc.code = script_code.c_str();
    // Own the outer capture. Native nested evaluation only returns captured
    // text to the outermost caller; its parent can otherwise lose include errors.
    ScriptLogCapture capture;
    const auto managed = ecs_script_init(world, &desc);
    const auto initialization_error = capture.finish();
    const auto* state = managed ? ecs_get(world, managed, EcsScript) : nullptr;
    // 4.1.6 init can return an entity after an evaluation error: inspect EcsScript.
    if (!state || state->error || !state->script)
        return script_error(!file_error.empty()             ? file_error
                            : !initialization_error.empty() ? initialization_error
                            : state && state->error ? state->error
                                                    : "Managed script initialization failed");
    script_code = candidate;
    std::string update_error;
    if (!previous.empty()) {
        ScriptLogCapture update_capture;
        const auto failed = ecs_script_update(world, managed, 0, candidate.c_str());
        update_error = update_capture.finish();
        if (failed) {
            state = ecs_get(world, managed, EcsScript);
            return script_error(!file_error.empty()     ? file_error
                                : !update_error.empty() ? update_error
                                : state && state->error ? state->error
                                                        : "Managed script update failed");
        }
    }
    // Native managed includes in 4.1.6 can return an entity carrying an error
    // without propagating it to the including script. Admit the complete managed
    // result, not just the top-level script. Failed candidates remain isolated.
    auto scripts = ecs_each_id(world, ecs_id(EcsScript));
    while (ecs_each_next(&scripts)) {
        const auto* states = ecs_field(&scripts, EcsScript, 0);
        for (int32_t i = 0; i < scripts.count; ++i) {
            if (states[i].error || (states[i].filename && !states[i].script)) {
                auto result = script_error(states[i].error         ? states[i].error
                                           : !update_error.empty() ? update_error
                                           : !initialization_error.empty()
                                               ? initialization_error
                                               : "Included managed script failed");
                if (states[i].filename)
                    result["source"] = states[i].filename;
                ecs_iter_fini(&scripts);
                return result;
            }
        }
    }
    if (!initialization_error.empty() || !update_error.empty())
        return script_error(!update_error.empty() ? update_error : initialization_error);
    Json generated = Json::array();
    std::set<ecs_entity_t> emitted;
    auto it = ecs_each_id(world, ecs_pair(ecs_id(EcsScript), EcsWildcard));
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; ++i) {
            if (!emitted.insert(it.entities[i]).second)
                continue;
            if (generated.size() >= 10000) {
                ecs_iter_fini(&it);
                return script_error("Script preview exceeds 10000 managed entities");
            }
            char* text = ecs_entity_to_json(world, it.entities[i], nullptr);
            if (!text) {
                ecs_iter_fini(&it);
                return script_error("Generated entity JSON could not be produced");
            }
            std::unique_ptr<char, decltype(api.free_)> owned(text, api.free_);
            generated.push_back(Json::parse(text));
        }
    }
    return {{"ok", true},
            {"source", script_locator},
            {"world", "isolated Preview"},
            {"ownership", "Flecs managed script"},
            {"managed_update", !previous.empty()},
            {"includes", included},
            {"entities", generated}};
}
Json preview_flecs_script(const std::filesystem::path& project, const std::filesystem::path& source,
                          const std::string& code, const std::string& previous,
                          const std::filesystem::path& executable, std::stop_token cancel) {
    ProjectPaths paths(project);
    const auto locator = ProjectPaths::normalize(source);
    (void)paths.resolve(locator);
    if (code.size() > text_limit || previous.size() > text_limit)
        throw std::runtime_error("Script exceeds 1 MiB");
    const auto staging = paths.cache() / "script" / EntityId::generate().str();
    std::filesystem::create_directories(staging);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{staging};
    atomic_write(staging / "request.json", Json{{"project", path_utf8(paths.root())},
                                                {"source", path_utf8(locator)},
                                                {"code", code},
                                                {"previous", previous}}
                                               .dump());
    asset_detail::run_worker(asset_detail::WorkerKind::Script, executable, staging, cancel);
    const auto output = staging / "result.json";
    if (!std::filesystem::is_regular_file(output) ||
        std::filesystem::file_size(output) > 16 * text_limit)
        throw std::runtime_error("Script worker returned no bounded result");
    std::ifstream file(output, std::ios::binary);
    auto result = Json::parse(file);
    if (!result.is_object() || !result.contains("ok") || !result.at("ok").is_boolean())
        throw std::runtime_error("Script worker returned an invalid result");
    return result;
}
} // namespace forge
