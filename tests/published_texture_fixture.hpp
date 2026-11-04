#pragma once
#include <algorithm>
#include <filesystem>
#include <forge/assets.hpp>
#include <stdexcept>
namespace forge::test {
// Copy publications only. Editable images are deliberately absent.
inline void published_texture_fixture(const std::filesystem::path& gallery,
                                      const std::filesystem::path& project) {
    const auto source = forge::AssetCatalog::open_project(gallery);
    auto catalog = forge::AssetCatalog::open_project(project);
    for (const char* locator :
         {"Assets/Sprites/Badge.png", "Assets/OcclusionNormals/RippleNormals.png",
          "Assets/ReflectionCapture/Reflection.dds", "Assets/ReflectionProbes/WarmRoom.png",
          "Assets/TexturePrecision/Normalized16.png"}) {
        const auto found =
            std::find_if(source.records().begin(), source.records().end(),
                         [&](const auto& row) { return row.second.source == locator; });
        if (found == source.records().end())
            throw std::runtime_error("Published Texture fixture is missing");
        const auto& record = found->second;
        const auto key = record.metadata.at("forge.import").at("key").get<std::string>();
        if (key.size() != 64 || !std::all_of(key.begin(), key.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }))
            throw std::runtime_error("Invalid fixture publication key");
        const auto parent = project / ".forge/cache/derived";
        std::filesystem::create_directories(parent);
        std::filesystem::copy(gallery / ".forge/cache/derived" / key, parent / key,
                              std::filesystem::copy_options::recursive);
        catalog.add(record);
        if (std::filesystem::exists(project / record.source))
            throw std::runtime_error("Source-hidden Texture fixture has editable image bytes");
    }
    catalog.save(project / "forge.assets.json");
}
} // namespace forge::test
