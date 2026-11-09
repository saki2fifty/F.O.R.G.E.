#pragma once
#include <array>
#include <forge/audio_routing.hpp>
#include <forge/input.hpp>
namespace forge {
// Stable application key for OS user data; not an asset/entity/document identity.
bool valid_application_id(std::string_view);
// Shared project.game defaults. User overrides are a separate document.
nlohmann::json default_game_settings(std::string application_id, std::string title);
void validate_game_settings(const nlohmann::json&);
// Optional project-owned UI backend choice; absent means the existing font backend.
inline constexpr unsigned ui_text_shaping_profile_version = 2;
bool ui_text_shaping_enabled(const nlohmann::json& game);
// Optional horizontal whole-character editing/selection in native UI text fields.
inline constexpr unsigned ui_grapheme_editing_profile_version = 3;
bool ui_grapheme_editing_enabled(const nlohmann::json& game);
// Audio block from admitted game defaults or resolved settings; legacy channels default to one.
std::array<float, 3> audio_channel_volumes(const nlohmann::json& audio);
AudioRouting audio_routing_settings(const nlohmann::json& audio);
AudioBusVolumes audio_bus_volumes(const nlohmann::json& audio);
// Defaults + admitted per-user display/audio/input overrides. No project mutation.
// Binding overrides are keyed by ActionId; names/kinds/identities stay project-owned.
nlohmann::json resolve_game_settings(const nlohmann::json& defaults, const nlohmann::json& user,
                                     const InputMap& project_input);
} // namespace forge
