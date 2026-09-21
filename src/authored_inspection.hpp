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
// Values are [{"value": stamped_component, "property_intent": bool}, ...].
// The worker verifies target against the currently loaded module declaration.
// This returns detached values only: one-scene history or one-prefab publication
// is an explicit subsequent owning-thread operation, never a cross-document edit.
nlohmann::json migrate_project_authoring(const std::filesystem::path& runtime,
                                         const std::filesystem::path& project,
                                         const std::string& expected_fingerprint,
                                         const nlohmann::json& source, const nlohmann::json& target,
                                         const nlohmann::json& rules, const nlohmann::json& values,
                                         std::stop_token stop = {});
// Fixed-command child entry. Cwd is private staging, not the authored project.
int authored_inspection_worker();
} // namespace forge::detail
