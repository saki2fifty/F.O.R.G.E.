#pragma once
#include <forge/project_lease.hpp>
#include <nlohmann/json.hpp>
#include <stop_token>
namespace forge::asset_detail {
// Project writer only. Independently locked worker markers prevent deleting
// staging still owned by a supervisor or its inherited child handle.
nlohmann::json cleanup_import_jobs(const ProjectLease&, std::stop_token stop = {});
} // namespace forge::asset_detail
