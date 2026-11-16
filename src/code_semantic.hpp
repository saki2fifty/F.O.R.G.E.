#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

struct SDL_Environment;

namespace forge {
inline constexpr std::array<std::string_view, 14> cpp_semantic_kinds{
    "completion",     "declaration", "signature",      "diagnostics", "hover",
    "symbols",        "references",  "typeDefinition", "format",      "definition",
    "implementation", "selection",   "rename",         "fixes"};
inline bool cpp_semantic_kind(std::string_view kind) {
    return std::find(cpp_semantic_kinds.begin(), cpp_semantic_kinds.end(), kind) !=
           cpp_semantic_kinds.end();
}
struct CppSemanticContext {
    std::filesystem::path project, directory;
    std::string generation;
    // Borrowed only during child launch; SDL copies the inherited environment.
    SDL_Environment* environment = nullptr;
};
// Byte offsets refer to the current UTF-8 draft, not widget visual columns.
nlohmann::json cpp_semantic_position(std::string_view, std::size_t offset);
std::size_t cpp_semantic_offset(std::string_view, const nlohmann::json& position);
inline void validate_cpp_rename_name(std::string_view name) {
    if (name.empty() || name.size() > 256 ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
        throw std::runtime_error(
            "Use a nonempty C++ name within 256 UTF-8 bytes, without spaces or controls");
    (void)nlohmann::json(std::string(name)).dump(); // Existing strict UTF-8 facility.
}
nlohmann::json query_cpp_semantics(const nlohmann::json& request);
int cpp_semantic_worker(const std::filesystem::path& input, const std::filesystem::path& output);
} // namespace forge
