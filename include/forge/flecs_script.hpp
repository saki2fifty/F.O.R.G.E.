#pragma once
#include <forge/assets.hpp>
#include <stop_token>
namespace forge {
struct FlecsScriptAsset {
    static constexpr const char* type = "flecs_script";
};
// Atomically publish a successful diagnostic preview. Rejection leaves both
// outputs unchanged; this is not a transaction inside a native Flecs world.
bool publish_flecs_script_preview(const nlohmann::json& candidate, const std::string& source,
                                  std::string& published_source, std::string& published_result);
std::string read_flecs_script_source(const std::filesystem::path& project,
                                     const std::filesystem::path& source);
AssetRecord register_flecs_script(const std::filesystem::path& project,
                                  const std::filesystem::path& source);
// Worker-only entry. Must run before any other Flecs world or OS initialization.
// Each request owns an isolated Preview world; it cannot mutate an authored scene.
nlohmann::json evaluate_flecs_script_worker(const nlohmann::json& request);
// Execute the exact packaged tools binary with fixed arguments and bounded resources.
// Returns a diagnostic snapshot. A caller replaces its previous snapshot only on success.
nlohmann::json preview_flecs_script(const std::filesystem::path& project,
                                    const std::filesystem::path& source, const std::string& code,
                                    const std::string& previous,
                                    const std::filesystem::path& executable,
                                    std::stop_token cancel = {});
} // namespace forge
