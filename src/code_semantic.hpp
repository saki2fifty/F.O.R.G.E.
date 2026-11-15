#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

struct SDL_Environment;

namespace forge {
struct CppSemanticContext {
    std::filesystem::path project, directory;
    std::string generation;
    // Borrowed only during child launch; SDL copies the inherited environment.
    SDL_Environment* environment = nullptr;
};
// Byte offsets refer to the current UTF-8 draft, not widget visual columns.
nlohmann::json cpp_semantic_position(std::string_view, std::size_t offset);
std::size_t cpp_semantic_offset(std::string_view, const nlohmann::json& position);
nlohmann::json query_cpp_semantics(const nlohmann::json& request);
int cpp_semantic_worker(const std::filesystem::path& input, const std::filesystem::path& output);
} // namespace forge
