#pragma once
#include "../asset_storage.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <forge/project_paths.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <sstream>
#include <vector>

namespace forge::ui {
inline constexpr std::size_t max_cpp_source_bytes = 1024 * 1024;
inline bool cpp_source_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return extension == ".cpp" || extension == ".hpp" || extension == ".h" || extension == ".cc" ||
           extension == ".cxx" || extension == ".hh" || extension == ".hxx" ||
           extension == ".inl" || extension == ".ipp" || extension == ".tpp";
}
inline std::filesystem::path cpp_source_path(const std::filesystem::path& root,
                                             const std::filesystem::path& locator) {
    auto normalized = ProjectPaths::normalize(locator);
    if (*normalized.begin() != "Native" || !cpp_source_extension(normalized))
        throw std::runtime_error("Choose a C++ source/header inside Native.");
    for (const auto& part : normalized)
        if (part == "Builds" || part == ".forge")
            throw std::runtime_error("Generated deployment/build files are not editable sources.");
    auto path = ProjectPaths(root).resolve(normalized);
    asset_storage::ordinary(path);
    return path;
}
inline void validate_cpp_text(const std::string& text) {
    if (text.size() > max_cpp_source_bytes || text.find('\0') != std::string::npos)
        throw std::runtime_error("C++ source must be UTF-8 text without NUL, at most 1 MiB.");
    (void)nlohmann::json(text).dump(); // Reuse JSON's strict UTF-8 validation.
}
inline std::string read_cpp_source(const std::filesystem::path& path) {
    auto text = asset_storage::read(path, max_cpp_source_bytes);
    if (!text)
        throw std::runtime_error("C++ source no longer exists.");
    validate_cpp_text(*text);
    return *text;
}
inline void save_cpp_source(const std::filesystem::path& root, const std::filesystem::path& locator,
                            const std::string& baseline, const std::string& text) {
    validate_cpp_text(text);
    const auto path = cpp_source_path(root, locator);
    if (read_cpp_source(path) != baseline)
        throw std::runtime_error(
            "Source changed outside FORGE. Reload before saving; your draft is retained.");
    asset_storage::replace(path, text);
}
inline void create_cpp_source(const std::filesystem::path& root, const std::string& name) {
    if (name.empty() || name.size() > 120 ||
        name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
            std::string::npos ||
        (std::filesystem::path(name).extension() != ".cpp" &&
         std::filesystem::path(name).extension() != ".hpp"))
        throw std::runtime_error("Use a simple filename ending in .cpp or .hpp.");
    const auto path = cpp_source_path(root, std::filesystem::path("Native") / name);
    if (std::filesystem::exists(path))
        throw std::runtime_error("That source already exists; it was not overwritten.");
    const auto cmake = ProjectPaths(root).resolve("Native/CMakeLists.txt");
    const auto original = asset_storage::read(cmake);
    if (!original || !std::filesystem::is_regular_file(root / "Native/forge.sdk-project.json"))
        throw std::runtime_error("Create a managed C++ gameplay project first.");
    const std::string include = "include(forge.sources.cmake OPTIONAL)";
    if (original->find(include) == std::string::npos) {
        // Only upgrade the exact FORGE starter, never a user-modified CMake recipe.
        const std::string legacy =
            "cmake_minimum_required(VERSION 3.24)\nproject(ForgeGameplay LANGUAGES "
            "CXX)\nfind_package(ForgeNativeSdk CONFIG REQUIRED)\nadd_library(gameplay MODULE "
            "gameplay.cpp)\ntarget_link_libraries(gameplay PRIVATE "
            "ForgeNativeSdk::Client)\nset_target_properties(gameplay PROPERTIES PREFIX \"\" "
            "BUILD_RPATH_USE_ORIGIN ON)\nforge_install_gameplay_runtime(gameplay NAME "
            "project.gameplay)\n";
        auto normalized = *original;
        std::erase(normalized, '\r');
        if (normalized != legacy)
            throw std::runtime_error(
                "Custom CMake recipe: add include(forge.sources.cmake OPTIONAL) after "
                "add_library(gameplay ...) before creating additional sources.");
        const auto position = normalized.find("target_link_libraries");
        normalized.insert(position, include + "\n");
        asset_storage::replace(cmake, normalized);
    }
    const auto manifest = ProjectPaths(root).resolve("Native/forge.sources.cmake");
    asset_storage::ordinary(manifest);
    auto entries =
        asset_storage::read(manifest).value_or("# FORGE-created additional source files.\n");
    if (name.ends_with(".cpp"))
        entries += "target_sources(gameplay PRIVATE \"${CMAKE_CURRENT_LIST_DIR}/" + name + "\")\n";
    // File first; a failed registration removes only our unchanged newly created file.
    const std::string initial =
        name.ends_with(".hpp") ? "#pragma once\n" : "// Project gameplay source.\n";
    asset_storage::replace(path, initial);
    try {
        if (name.ends_with(".cpp"))
            asset_storage::replace(manifest, entries);
    } catch (...) {
        if (asset_storage::read(path).value_or("") == initial)
            asset_storage::erase_file(path);
        throw;
    }
}
inline std::vector<std::string> cpp_project_sources(const std::filesystem::path& root) {
    std::vector<std::string> result;
    const auto native = ProjectPaths(root).resolve("Native");
    if (!std::filesystem::is_directory(native))
        return result;
    for (std::filesystem::recursive_directory_iterator it(native), end; it != end; ++it) {
        const auto filename = it->path().filename();
        if (it->is_directory()) {
            if (filename == "Builds" || filename == ".forge" || it->is_symlink())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink() || !it->is_regular_file() || !cpp_source_extension(it->path()))
            continue;
        const auto relative = ProjectPaths(root).relative(it->path());
        (void)cpp_source_path(root, relative);
        result.push_back(path_utf8(relative));
        if (result.size() > 2048)
            throw std::runtime_error("Too many C++ gameplay source files.");
    }
    std::sort(result.begin(), result.end());
    return result;
}
inline void gameplay_identifier(const std::string& name) {
    if (name.empty() || name.size() > 64 ||
        !std::isalpha(static_cast<unsigned char>(name.front())) ||
        !std::all_of(name.begin(), name.end(),
                     [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
        throw std::runtime_error(
            "Use a C++ name starting with a letter, then letters, digits or _.");
}
inline void append_managed_registration(const std::filesystem::path& root,
                                        const std::string& marker, const std::string& line) {
    const auto path = ProjectPaths(root).resolve("Native/forge.registration.hpp");
    auto original = asset_storage::read(path);
    if (!original)
        throw std::runtime_error("This project predates C++ Component/System creation. Existing "
                                 "gameplay still builds; add the managed registration header to "
                                 "gameplay.cpp or create a new C++ gameplay project.");
    const auto position = original->find(marker);
    if (position == std::string::npos ||
        original->find(marker, position + marker.size()) != std::string::npos)
        throw std::runtime_error("Managed gameplay registration markers are unavailable.");
    original->insert(position, line + "\n    ");
    asset_storage::replace(path, *original);
}
inline std::string create_cpp_component(const std::filesystem::path& root,
                                        const std::string& name) {
    gameplay_identifier(name);
    const auto locator = std::filesystem::path("Native/Components") / (name + ".hpp");
    const auto path = cpp_source_path(root, locator);
    if (std::filesystem::exists(path))
        throw std::runtime_error("C++ component source already exists.");
    auto registry =
        asset_storage::read(ProjectPaths(root).resolve("Native/forge.registration.hpp"));
    if (!registry || registry->find("// FORGE_COMPONENT_INCLUDES") == std::string::npos ||
        registry->find("// FORGE_COMPONENT_CALLS") == std::string::npos)
        throw std::runtime_error("This project has no managed C++ registration header.");
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (registry->find("// FORGE_SOURCE_COMPONENT project." + key + " ") != std::string::npos)
        throw std::runtime_error("A C++ component already owns this stable type key.");
    std::ostringstream source;
    source << "#pragma once\n#include <cstdio>\n#include <exception>\n#include <flecs.h>\n"
              "#include <forge/native_sdk.h>\n\n"
           << "// Data attached to entities. A System supplies behavior.\n"
           << "struct " << name << " { float speed = 90.0f; };\n\n"
           << "inline int32_t forge_register_component_" << name
           << "(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {\n"
              "    try {\n        flecs::world world(host->world);\n"
           << "        auto type = world.component<" << name << ">(\"gameplay." << name
           << "\").member<float>(\"speed\");\n"
           << "        ecs_doc_set_name(world, type, \"" << name
           << "\");\n"
              "        return host->authoring_type(host->context, type, \"project."
           << key
           << "\", 1, R\"({\"speed\":90})\", \"Gameplay\", error, capacity);\n"
              "    } catch (const std::exception& e) {\n"
              "        if (capacity) std::snprintf(error, capacity, \"%s\", e.what());\n"
              "        return 0;\n"
              "    } catch (...) {\n"
              "        if (capacity) std::snprintf(error, capacity, \"Registration failed\");\n"
              "        return 0;\n    }\n}\n";
    std::filesystem::create_directories(path.parent_path());
    asset_storage::replace(path, source.str());
    try {
        append_managed_registration(root, "// FORGE_COMPONENT_INCLUDES",
                                    "// FORGE_SOURCE_COMPONENT project." + key +
                                        " Native/Components/" + name + ".hpp\n" +
                                        "#include \"Components/" + name + ".hpp\"");
        append_managed_registration(root, "// FORGE_COMPONENT_CALLS",
                                    "if (!forge_register_component_" + name +
                                        "(host, error, capacity)) return 0;");
    } catch (...) {
        // The source remains as an editable draft if a later registry write failed.
        throw;
    }
    return path_utf8(locator);
}
inline std::string create_cpp_system(const std::filesystem::path& root, const std::string& name,
                                     const std::string& component) {
    gameplay_identifier(name);
    gameplay_identifier(component);
    if (!std::filesystem::is_regular_file(cpp_source_path(
            root, std::filesystem::path("Native/Components") / (component + ".hpp"))))
        throw std::runtime_error("Create the C++ Component before its System.");
    const auto locator = std::filesystem::path("Native/Systems") / (name + ".cpp");
    const auto path = cpp_source_path(root, locator);
    if (std::filesystem::exists(path))
        throw std::runtime_error("C++ system source already exists.");
    const auto registry =
        asset_storage::read(ProjectPaths(root).resolve("Native/forge.registration.hpp"));
    const auto manifest = ProjectPaths(root).resolve("Native/forge.sources.cmake");
    if (!registry || registry->find("// FORGE_SYSTEM_DECLARATIONS") == std::string::npos ||
        registry->find("// FORGE_SYSTEM_CALLS") == std::string::npos)
        throw std::runtime_error("This project has no managed C++ registration header.");
    std::ostringstream source;
    source << "#include \"../Components/" << component
           << ".hpp\"\n"
              "#include <cmath>\n#include <flecs.h>\n#include <forge/native_sdk.h>\n"
              "#include <forge/transform_components.hpp>\n\n"
           << "// Runs for every entity with " << component
           << ". Missing rotation starts at identity.\n"
           << "void forge_register_system_" << name
           << "(const ForgeSdkWorldV1* host) {\n"
              "    flecs::world world(host->world);\n"
           << "    world.system<const " << component << ">(\"gameplay." << name
           << "\")\n"
              "        .kind(host->fixed_phase)\n"
           << "        .each([](flecs::iter& it, size_t row, const " << component
           << "& data) {\n"
              "            auto entity = it.entity(row);\n"
              "            const auto q = entity.has<forge::LocalRotation>()\n"
              "                ? entity.get<forge::LocalRotation>() : forge::LocalRotation{};\n"
              "            if (!std::isfinite(data.speed)) return;\n"
              "            const double half = double(data.speed) * it.delta_time() *\n"
              "                                0.008726646259971648; // degrees / 2 to radians\n"
              "            const double s = std::sin(half), c = std::cos(half);\n"
              "            forge::LocalRotation next{float(q.x*c - q.z*s),\n"
              "                                      float(q.w*s + q.y*c),\n"
              "                                      float(q.z*c + q.x*s),\n"
              "                                      float(q.w*c - q.y*s)};\n"
              "            const double length = std::sqrt(double(next.x)*next.x +\n"
              "                double(next.y)*next.y + double(next.z)*next.z + "
              "double(next.w)*next.w);\n"
              "            if (length > 0 && std::isfinite(length)) {\n"
              "                next.x /= float(length); next.y /= float(length);\n"
              "                next.z /= float(length); next.w /= float(length);\n"
              "                entity.set<forge::LocalRotation>(next);\n"
              "            }\n"
              "        }).add(host->fixed_tag);\n}\n";
    std::filesystem::create_directories(path.parent_path());
    asset_storage::replace(path, source.str());
    const auto source_line =
        "target_sources(gameplay PRIVATE \"${CMAKE_CURRENT_LIST_DIR}/Systems/" + name + ".cpp\")\n";
    auto sources =
        asset_storage::read(manifest).value_or("# FORGE-created additional source files.\n");
    if (sources.find(source_line) != std::string::npos)
        throw std::runtime_error("C++ system source is already registered.");
    asset_storage::replace(manifest, sources + source_line);
    append_managed_registration(root, "// FORGE_SYSTEM_DECLARATIONS",
                                "// FORGE_SOURCE_SYSTEM " + name + " Native/Systems/" + name +
                                    ".cpp " + component + "\n" + "void forge_register_system_" +
                                    name + "(const ForgeSdkWorldV1* host);");
    append_managed_registration(root, "// FORGE_SYSTEM_CALLS",
                                "forge_register_system_" + name + "(host);");
    return path_utf8(locator);
}

// Wizard-authored provenance lives beside actual registration. It is deliberately
// narrow: arbitrary C++ registrations have no invented source mapping.
inline std::optional<std::string> managed_component_definition(const std::filesystem::path& root,
                                                               const std::string& key) {
    if (!key.starts_with("project.") || key.size() > 80)
        return std::nullopt;
    const auto registry =
        asset_storage::read(ProjectPaths(root).resolve("Native/forge.registration.hpp"));
    if (!registry)
        return std::nullopt;
    std::istringstream lines(*registry);
    std::string line;
    const auto prefix = "// FORGE_SOURCE_COMPONENT " + key + " ";
    while (std::getline(lines, line)) {
        if (!line.starts_with(prefix))
            continue;
        const auto locator = line.substr(prefix.size());
        if (locator.find_first_of("\r\n \t") != std::string::npos)
            return std::nullopt;
        try {
            const auto path = cpp_source_path(root, locator);
            if (std::filesystem::is_regular_file(path) &&
                registry->find("#include \"Components/" + path.stem().string() + ".hpp\"") !=
                    std::string::npos)
                return locator;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

struct ManagedSystemSource {
    std::string name, source, component;
};
inline std::vector<ManagedSystemSource> managed_system_sources(const std::filesystem::path& root) {
    const auto registry =
        asset_storage::read(ProjectPaths(root).resolve("Native/forge.registration.hpp"));
    std::vector<ManagedSystemSource> result;
    if (!registry)
        return result;
    std::istringstream lines(*registry);
    std::string line;
    constexpr std::string_view prefix = "// FORGE_SOURCE_SYSTEM ";
    while (result.size() < 128 && std::getline(lines, line)) {
        if (!line.starts_with(prefix))
            continue;
        std::istringstream fields(line.substr(prefix.size()));
        ManagedSystemSource entry;
        std::string extra;
        if (!(fields >> entry.name >> entry.source >> entry.component) || fields >> extra)
            continue;
        try {
            if (std::filesystem::is_regular_file(cpp_source_path(root, entry.source)) &&
                registry->find("forge_register_system_" + entry.name + "(host);") !=
                    std::string::npos)
                result.push_back(std::move(entry));
        } catch (const std::exception&) {
        }
    }
    return result;
}

struct CppDiagnostic {
    std::string source, message;
    int line = 0, column = 1;
};
inline std::vector<CppDiagnostic> cpp_diagnostics(const std::filesystem::path& root,
                                                  const std::string& log) {
    static const std::regex msvc(R"(^(.+)\(([0-9]+)(?:,([0-9]+))?\)\s*:\s*(.*)$)");
    static const std::regex clang(R"(^(.+?):([0-9]+)(?::([0-9]+))?:\s*(.*)$)");
    std::vector<CppDiagnostic> result;
    std::istringstream lines(log);
    std::string line;
    while (result.size() < 256 && std::getline(lines, line)) {
        if (line.size() > 8192 ||
            (line.find("error") == std::string::npos && line.find("warning") == std::string::npos))
            continue;
        std::smatch match;
        if (!std::regex_match(line, match, msvc) && !std::regex_match(line, match, clang))
            continue;
        auto integer = [](const std::string& value) {
            int number = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                           number > 0
                       ? number
                       : 0;
        };
        try {
            const auto path = std::filesystem::u8path(match[1].str());
            const auto locator = path.is_absolute() ? ProjectPaths(root).relative(path)
                                                    : ProjectPaths::normalize(path);
            (void)cpp_source_path(root, locator);
            const int row = integer(match[2].str());
            const int column = match[3].matched ? integer(match[3].str()) : 1;
            if (row && column)
                result.push_back({path_utf8(locator), match[4].str(), row, column});
        } catch (const std::exception&) {
            // SDK/generated/external diagnostics remain visible in raw build output.
        }
    }
    return result;
}
} // namespace forge::ui
