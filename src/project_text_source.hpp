#pragma once
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include <forge/authoring.hpp>
#include <forge/project_paths.hpp>
#include <forge/ui_assets.hpp>

namespace forge {
// Shared raw-file admission/storage, without a draft, history or catalog owner.
// Callers hold the project writer lease and validate the relative locator first.
struct ProjectTextSource {
    static constexpr std::size_t limit = 1024 * 1024;
    static bool allowed_root(const std::filesystem::path& path) {
        if (path.empty())
            return false;
        const auto first = path.begin()->string();
        return first == "Assets" || first == "Sources" || first == "UI" || first == "Shaders";
    }
    static bool source_extension(const std::filesystem::path& path) {
        const auto extension = path.extension();
        return extension == ".rml" || extension == ".rcss" || extension == ".hlsl" ||
               extension == ".hlsli" || path_utf8(path).ends_with(".shader.json");
    }
    static std::filesystem::path path(const std::filesystem::path& root,
                                      const std::filesystem::path& locator) {
        if (!allowed_root(locator) || !source_extension(locator))
            throw AuthoringError(
                "unsupported_source",
                "Choose project UI/shader source under Assets, Sources, UI or Shaders");
        const ProjectPaths project(root);
        // Check before canonical resolution can hide an alias into another owner.
        asset_storage::ordinary(project.root() / locator);
        return project.resolve(locator);
    }
    static std::string digest(const std::string& text) {
        return asset_detail::content_digest(std::as_bytes(std::span(text.data(), text.size())));
    }
    static std::optional<std::string> read(const std::filesystem::path& path) {
        const auto text = asset_storage::read(path, limit);
        if (text) {
            if (text->find('\0') != std::string::npos)
                throw AuthoringError("invalid_source", "Source must be UTF-8 text without NUL");
            (void)Json(*text).dump();
        }
        return text;
    }
    static void validate_text(const std::string& text) {
        if (text.size() > limit || text.find('\0') != std::string::npos)
            throw AuthoringError("invalid_source",
                                 "Source must be UTF-8 text without NUL, at most1MiB");
        (void)Json(text).dump();
    }
    static void validate(const std::filesystem::path& path, const std::string& text) {
        validate_text(text);
        if (path.extension() == ".rml" || path.extension() == ".rcss")
            validate_ui_text(text, path.extension() == ".rml");
    }
    // The caller validates its complete candidate, including any domain identity,
    // before publication. Recheck the disk baseline at this shared storage boundary.
    static void write_validated(const std::filesystem::path& path,
                                const std::optional<std::string>& before, const std::string& text) {
        if (read(path) != before)
            throw AuthoringError("source_conflict", "Source changed; read it before writing");
        if (!std::filesystem::is_directory(path.parent_path()))
            throw AuthoringError("invalid_path", "Choose an existing source directory");
        asset_storage::replace(path, text);
    }
};
} // namespace forge
