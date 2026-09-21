#pragma once
#include <forge/project_lease.hpp>
#include <set>
#include <stop_token>
#include <vector>
namespace forge {
struct SourceCopyFile {
    std::filesystem::path original, destination;
    std::string digest;
    std::uint64_t bytes = 0;
};
struct SourceCopyPlan {
    std::filesystem::path project, destination;
    std::vector<SourceCopyFile> files;
    std::vector<std::filesystem::path> sources;
    std::uint64_t bytes = 0;
};
// Copies only explicitly selected raw sources and admitted glTF dependencies.
// Each selection gets its own subfolder; no sidecar/AssetId is copied. Preparation
// is read-only and cancellable, bounded to256 roots/8192 files/2GiB aggregate.
SourceCopyPlan prepare_source_copy(const std::filesystem::path& project,
                                   const std::filesystem::path& new_folder,
                                   const std::vector<std::filesystem::path>& files,
                                   const std::set<std::string>& gltf_extensions = {},
                                   std::stop_token stop = {});
// Worker-owned IO using the retained project lease. Rechecks every reviewed byte
// revision; atomically creates a NEW directory without replacing an existing one.
// Source copy and subsequent individual asset imports are separate operations.
void commit_source_copy(const ProjectLease&, const SourceCopyPlan&, std::stop_token = {});
} // namespace forge
