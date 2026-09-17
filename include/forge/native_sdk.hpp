#pragma once
#include <filesystem>
#include <forge/engine_module.hpp>
namespace forge {
// Loads and validates only; registration occurs through WorldContext bootstrap.
// Returned descriptor retains code. No replacement/unload operation exists.
EngineModule load_native_sdk(const std::filesystem::path&, const std::string& expected_id,
                             const std::string& implementation, ServiceAccess = {});
std::vector<EngineModule> project_native_modules(const std::filesystem::path& root,
                                                 const nlohmann::json& project, ServiceAccess = {});
void validate_project_modules(const nlohmann::json& project);
} // namespace forge
