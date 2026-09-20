#pragma once
#include "import_process.hpp"
#include <forge/asset_importer.hpp>
#include <forge/shader_asset.hpp>
namespace forge::asset_detail {
struct ShaderCompilerProfile {
    std::string digest;
    bool debug = false;
};
struct ShaderSnapshot {
    nlohmann::json document;
    std::string document_digest;
    ShaderProgramSource program;
    ShaderSources sources;
    // Virtual source name -> actual project locator. Engine includes are captured
    // host inputs and never pretend to be project files in the dependency index.
    std::map<std::string, std::filesystem::path> project_sources;
};
ShaderSnapshot capture_shader_source(const std::filesystem::path& project,
                                     const std::filesystem::path& document,
                                     const ShaderSources& engine_sources = {},
                                     std::stop_token stop = {});
AssetBuildInput shader_import_input(const ShaderSnapshot&,
                                    const std::map<std::string, std::string>& permutation,
                                    const ShaderCompilerProfile&);
std::string shader_import_revision(const ShaderCompilerProfile&, const ShaderSources& engine);
ImportProcessRequest shader_process_request(const ShaderSnapshot&,
                                            const std::map<std::string, std::string>& permutation,
                                            const ShaderCompilerProfile&);
struct ShaderProcessInput {
    ShaderProgramSource program;
    ShaderSources sources;
    std::map<std::string, std::string> permutation;
    std::string build_key;
};
ShaderProcessInput decode_shader_process_request(const ImportProcessRequest&,
                                                 const ShaderCompilerProfile& actual);
WorkerLimits shader_worker_limits();
std::shared_ptr<const AssetImporter> shader_importer(std::filesystem::path worker,
                                                     ShaderCompilerProfile,
                                                     ShaderSources engine_sources = {});
} // namespace forge::asset_detail
