#pragma once
#include "worker_stage_lease.hpp"
#include <forge/project_lease.hpp>
#include <nlohmann/json.hpp>
#include <stop_token>
namespace forge::asset_detail {
// One temporary-storage owner for the existing fixed worker layouts. No catalog
// identity or publication authority; a candidate keeps this through publication.
struct WorkerStaging {
    std::filesystem::path path;
    std::unique_ptr<WorkerStageLease> owner;
    WorkerStaging(const std::filesystem::path& project, std::string_view kind);
    ~WorkerStaging();
    WorkerStaging(const WorkerStaging&) = delete;
    WorkerStaging& operator=(const WorkerStaging&) = delete;
};
// Producer only: the exact unique unpublished stage it allocated. Release the
// producer's lease first; a worker retaining its copy still prevents removal.
// Uses the same ownership/layout checks as project-wide maintenance.
nlohmann::json cleanup_worker_stage(const std::filesystem::path&, std::stop_token stop = {});
// Project writer only. Independently locked worker markers prevent deleting
// staging still owned by a supervisor or its designated worker handle.
nlohmann::json cleanup_worker_stages(const ProjectLease&, std::stop_token stop = {});
} // namespace forge::asset_detail
