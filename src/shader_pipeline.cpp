#include "shader_pipeline.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t document_limit = 1024 * 1024;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void cancelled(std::stop_token stop) { require(!stop.stop_requested(), "Shader import cancelled"); }
bool source_extension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    for (auto& c : ext)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return ext == ".hlsl" || ext == ".hlsli" || ext == ".fx" || ext == ".fxh" || ext == ".inc";
}
std::string text(const ArtifactFile& file) {
    return {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()};
}
void validate_engine_sources(const ShaderSources& engine) {
    if (!engine.empty())
        validate_shader_sources(engine);
    for (const auto& [name, bytes] : engine) {
        (void)bytes;
        require(name.starts_with("engine/"), "Engine shader sources require the engine/ namespace");
    }
}
} // namespace
ShaderSnapshot capture_shader_source(const std::filesystem::path& project,
                                     const std::filesystem::path& locator,
                                     const ShaderSources& engine, std::stop_token stop) {
    cancelled(stop);
    validate_engine_sources(engine);
    const ProjectPaths paths(project);
    const auto bytes = read_bytes(paths.resolve(ProjectPaths::normalize(locator)), document_limit);
    ShaderSnapshot result;
    result.document = parse_bounded_json(bytes, document_limit);
    result.document_digest = content_digest(bytes);
    result.program = shader_program_source(result.document);
    const auto root_name = result.document.at("source_root").get<std::string>();
    const auto root = ProjectPaths::normalize(std::filesystem::u8path(root_name));
    require(path_utf8(root) == root_name, "Shader source root must be a canonical project locator");
    const auto absolute = paths.resolve(root);
    require(std::filesystem::is_directory(absolute), "Shader source root is not a directory");
    result.sources = engine;
    std::size_t entries = 0, total = 0;
    for (const auto& [name, source] : engine) {
        (void)name;
        total += source.size();
    }
    for (const auto& file : std::filesystem::recursive_directory_iterator(absolute)) {
        cancelled(stop);
        require(++entries <= 4096, "Shader source root exceeds directory entry budget");
        const auto status = file.symlink_status();
        require(!std::filesystem::is_symlink(status),
                "Shader source root contains a filesystem link");
        if (std::filesystem::is_directory(status))
            continue;
        require(std::filesystem::is_regular_file(status),
                "Shader source root contains a special file");
        if (!source_extension(file.path()))
            continue;
        const auto relative = file.path().lexically_relative(absolute);
        const auto name = path_utf8(relative);
        require(!name.starts_with("engine/"),
                "Project shader sources cannot shadow engine/ includes");
        require(result.sources.size() < 256, "Shader source snapshot exceeds file budget");
        const auto source_path = ProjectPaths::normalize(root / relative);
        const auto source = read_bytes(paths.resolve(source_path), 2 * 1024 * 1024);
        total += source.size();
        require(total <= 16 * 1024 * 1024, "Shader source snapshot exceeds byte budget");
        require(result.sources
                    .emplace(name, std::string(reinterpret_cast<const char*>(source.data()),
                                               source.size()))
                    .second,
                "Duplicate shader source alias");
        result.project_sources.emplace(name, source_path);
    }
    validate_shader_sources(result.sources);
    for (const auto& entry : result.program.stages)
        require(result.sources.contains(entry.source),
                "Shader stage source is absent from its root");
    cancelled(stop);
    return result;
}
AssetBuildInput shader_import_input(const ShaderSnapshot& snapshot,
                                    const std::map<std::string, std::string>& permutation,
                                    const ShaderCompilerProfile& compiler) {
    auto input = shader_build_input(snapshot.program, snapshot.sources, permutation,
                                    compiler.digest, compiler.debug);
    input.source_digest = snapshot.document_digest;
    input.source_dependencies.clear();
    for (const auto& [name, path] : snapshot.project_sources)
        input.source_dependencies[path_utf8(path)] =
            content_digest(std::as_bytes(std::span(snapshot.sources.at(name))));
    ShaderSources engine;
    for (const auto& [name, source] : snapshot.sources)
        if (!snapshot.project_sources.contains(name))
            engine.emplace(name, source);
    input.importer_revision = shader_import_revision(compiler, engine);
    input.settings = {{"permutation", permutation}};
    (void)input.key();
    return input;
}
std::string shader_import_revision(const ShaderCompilerProfile& compiler,
                                   const ShaderSources& engine) {
    validate_engine_sources(engine);
    Json files = Json::object();
    for (const auto& [name, source] : engine)
        files[name] = content_digest(std::as_bytes(std::span(source)));
    return asset_build_digest({{"recipe", "forge.shader.import.v1"},
                               {"compiler_adapter", shader_compiler_revision()},
                               {"core", "744f079f61cdbda15d371383682418fc927e4a61"},
                               {"compiler_debug", compiler.debug},
                               {"engine_sources", files}});
}
ImportProcessRequest shader_process_request(const ShaderSnapshot& snapshot,
                                            const std::map<std::string, std::string>& permutation,
                                            const ShaderCompilerProfile& compiler) {
    const auto input = shader_build_input(snapshot.program, snapshot.sources, permutation,
                                          compiler.digest, compiler.debug);
    ImportProcessRequest result;
    result.payload = {{"recipe", "forge.shader.diligent"}, {"revision", input.importer_revision},
                      {"document", snapshot.document},     {"permutation", permutation},
                      {"compiler", compiler.digest},       {"debug", compiler.debug},
                      {"build_key", input.key()},          {"sources", Json::object()}};
    for (const auto& [name, source] : snapshot.sources) {
        const auto file = "source-" + std::to_string(result.inputs.size()) + ".hlsl";
        const auto bytes = std::as_bytes(std::span(source));
        result.inputs.push_back({file, {bytes.begin(), bytes.end()}});
        result.payload["sources"][name] = file;
    }
    return result;
}
ShaderProcessInput decode_shader_process_request(const ImportProcessRequest& request,
                                                 const ShaderCompilerProfile& actual) {
    const auto& p = request.payload;
    require(p.at("recipe") == "forge.shader.diligent" && p.at("compiler") == actual.digest &&
                p.at("debug") == actual.debug,
            "Shader worker/compiler profile changed since discovery");
    ShaderProcessInput result;
    result.program = shader_program_source(p.at("document"));
    result.permutation = p.at("permutation").get<std::map<std::string, std::string>>();
    require(p.at("sources").is_object() && !p.at("sources").empty() &&
                p.at("sources").size() <= 256 && request.inputs.size() == p.at("sources").size(),
            "Shader worker source count mismatch");
    std::map<std::string, const ArtifactFile*> inputs;
    for (const auto& file : request.inputs)
        require(inputs.emplace(file.name, &file).second, "Duplicate shader worker file");
    for (const auto& [name, file] : p.at("sources").items()) {
        const auto found = inputs.find(file.get<std::string>());
        require(found != inputs.end() && found->second->bytes.size() <= 2 * 1024 * 1024,
                "Missing/reused/oversized shader worker source");
        result.sources.emplace(name, text(*found->second));
        inputs.erase(found);
    }
    const auto input = shader_build_input(result.program, result.sources, result.permutation,
                                          actual.digest, actual.debug);
    result.build_key = input.key();
    require(inputs.empty() && p.at("build_key") == result.build_key &&
                p.at("revision") == input.importer_revision,
            "Shader worker snapshot/revision mismatch");
    return result;
}
WorkerLimits shader_worker_limits() {
    WorkerLimits limits;
    limits.memory_bytes = 1024ull * 1024 * 1024;
    limits.file_bytes = 20ull * 1024 * 1024;
    limits.total_bytes = 32ull * 1024 * 1024;
    limits.files = 260;
    limits.seconds = 60;
    limits.cpu_seconds = 50;
    limits.cancellation_grace_ms = 100;
    return limits;
}
} // namespace forge::asset_detail
