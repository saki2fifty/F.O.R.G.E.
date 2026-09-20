#pragma once
#include "asset_worker.hpp"
#include <forge/derived_cache.hpp>
namespace forge::asset_detail {
struct ImportProcessRequest {
    nlohmann::json payload;
    std::vector<ArtifactFile> inputs;
};
// Private versioned process transport, not an SDK/plugin protocol. The executable
// is the packaged fixed-command worker; all inputs are immutable owned snapshots.
std::vector<ArtifactFile> run_import_process(const std::filesystem::path& executable,
                                             const std::filesystem::path& project,
                                             ImportProcessRequest request, WorkerLimits limits,
                                             std::stop_token stop = {});
// Second sequential process for model animation. Only canonical flat glTF input
// and a fixed official converter command are accepted; no catalog writes.
std::vector<ArtifactFile> run_model_animation_process(const std::filesystem::path& executable,
                                                      const std::filesystem::path& project,
                                                      std::span<const ArtifactFile> inputs,
                                                      std::stop_token stop = {});
ImportProcessRequest read_import_process_request(const std::filesystem::path& staging,
                                                 WorkerLimits limits);
// Bounded header inspection selects one of the executable's fixed recipes before
// allocating input buffers. The request cannot choose its own resource limits.
std::string read_import_process_recipe(const std::filesystem::path& staging);
void write_import_process_result(const std::filesystem::path& staging,
                                 const std::vector<ArtifactFile>& files, WorkerLimits limits);
void write_import_process_error(const std::filesystem::path& staging, std::string_view code,
                                std::string_view message) noexcept;
} // namespace forge::asset_detail
