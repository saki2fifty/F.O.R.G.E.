#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stop_token>
namespace forge::detail {
// Blocking worker orchestration: call on a cancellable task, never the editor UI
// thread. Fixed SDK command, isolated lifetime, copied metadata only. This does
// transport/provenance checks, not live schema publication: the owner thread must
// call validate_authored_types/reconstruct before accepting any copied type.
nlohmann::json inspect_project_authoring(const std::filesystem::path& runtime,
                                         const std::filesystem::path& project,
                                         const std::string& expected_fingerprint,
                                         std::stop_token stop = {});
nlohmann::json export_project_authoring(const std::filesystem::path& project);
// Fixed-command child entry. Cwd is private staging, not the authored project.
int authored_inspection_worker();
} // namespace forge::detail
