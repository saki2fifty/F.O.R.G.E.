#pragma once
#include <forge/input.hpp>
namespace forge {
// Stable application key for OS user data; not an asset/entity/document identity.
bool valid_application_id(std::string_view);
// Shared project.game defaults. User overrides are a separate document.
nlohmann::json default_game_settings(std::string application_id, std::string title);
void validate_game_settings(const nlohmann::json&);
// Defaults + admitted per-user display/audio/input overrides. No project mutation.
// Binding overrides are keyed by ActionId; names/kinds/identities stay project-owned.
nlohmann::json resolve_game_settings(const nlohmann::json& defaults, const nlohmann::json& user,
                                     const InputMap& project_input);
} // namespace forge
