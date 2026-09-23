#pragma once
#include <filesystem>
namespace forge {
// SDL's OS writable base for GameStorage. The storage owner adds game-APPID.
// No fallback to installation/project/current-working directory on failure.
std::filesystem::path game_user_data_base();
} // namespace forge
