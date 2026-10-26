#pragma once
#include <forge/project_lease.hpp>
#include <nlohmann/json.hpp>
#include <stop_token>
namespace forge::asset_detail {
// Producer only: the exact unique unpublished stage it allocated. Release the
// producer's lease first; a worker retaining its copy still prevents removal.
// Uses the same ownership/layout checks as project-wide maintenance.
nlohmann::json cleanup_import_job(const std::filesystem::path&, std::stop_token stop = {});
// Project writer only. Independently locked worker markers prevent deleting
// staging still owned by a supervisor or its designated worker handle.
nlohmann::json cleanup_import_jobs(const ProjectLease&, std::stop_token stop = {});
} // namespace forge::asset_detail
