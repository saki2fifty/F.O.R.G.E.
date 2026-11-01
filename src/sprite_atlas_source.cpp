#include "sprite_atlas_source.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include "model_render_resource.hpp"
#include "sprite_material_profile.hpp"
#include <algorithm>
#include <set>

namespace forge::asset_detail {
SpriteAtlasCapture capture_sprite_atlas_images(const std::filesystem::path& project,
                                               const AssetCatalog& catalog,
                                               std::span<const AssetId> textures,
                                               SpriteAtlasPacking settings, std::stop_token stop) {
    (void)sprite_atlas_settings(settings);
    if (textures.empty() || textures.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Select between1 and512 published image textures");
    const auto check_stop = [&] {
        if (stop.stop_requested())
            throw std::runtime_error("Atlas source generation cancelled");
    };
    check_stop();
    std::set<AssetId> seen;
    std::vector<SpriteFrameRegion> regions;
    SpriteAtlasCapture result;
    auto remaining = settings.byte_limit;
    for (const auto id : textures) {
        check_stop();
        if (!id || !seen.insert(id).second)
            throw std::invalid_argument("Atlas inputs need distinct current Texture AssetIds");
        const auto found = catalog.records().find(id);
        if (found == catalog.records().end() || found->second.type != TextureAsset::type ||
            found->second.subasset || !found->second.metadata.contains("forge.import"))
            throw std::invalid_argument(
                "Atlas inputs must be published independent image textures; import them first");
        TextureLimits limits;
        limits.bytes = remaining;
        limits.dimension = settings.trim_transparent ? 16384 : settings.max_size;
        auto candidate =
            load_texture_selection(project, catalog, {id}, TextureSemantic::Color, limits, stop);
        auto texture = std::move(*candidate.value);
        validate_sprite_texture_profile(texture);
        if (texture.format != TextureFormat::RGBA8Srgb)
            throw std::invalid_argument("Atlas PNG generation requires sRGB RGBA8 color images");
        remaining -= texture.byte_size();
        regions.push_back(sprite_atlas_image_region(texture, settings, stop));
        const auto revision =
            found->second.metadata.at("forge.import").at("key").get<std::string>();
        if (!valid_content_digest(revision))
            throw std::invalid_argument("Atlas input has no immutable published revision");
        result.dependencies.push_back({id, TextureAsset::type, AssetDependencyKind::Build,
                                       "atlas.image." + id.str(), revision});
        // Only admitted base pixels are sent; ordinary source mips are not
        // interpreted as a valid packed atlas mip chain.
        texture.sprite_frames.clear();
        texture.mips = 1;
        texture.subresources.resize(1);
        result.images.push_back(
            {sprite_atlas_input_name(result.images.size()), encode_texture(texture, limits)});
    }
    result.layout = pack_sprite_atlas_regions(regions, settings, stop);
    return result;
}
SpriteAtlasSource prepare_sprite_atlas_source(const std::filesystem::path& worker,
                                              const std::filesystem::path& project,
                                              std::span<const AssetId> textures,
                                              SpriteAtlasPacking settings, std::stop_token stop) {
    const auto options = sprite_atlas_settings(settings);
    if (textures.empty() || textures.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Select between1 and512 published image textures");
    const auto check_stop = [&] {
        if (stop.stop_requested())
            throw std::runtime_error("Atlas source generation cancelled");
    };
    check_stop();
    const auto index = AssetCatalog::project_index(project);
    asset_storage::ordinary(index);
    auto bytes = read_bytes(index, max_asset_index_bytes);
    SpriteAtlasSource result;
    result.catalog_digest = content_digest(bytes);
    AssetCatalog catalog(project);
    catalog.restore(parse_bounded_json(bytes, max_asset_index_bytes));
    bytes.clear();
    bytes.shrink_to_fit();
    auto capture = capture_sprite_atlas_images(project, catalog, textures, settings, stop);
    const auto& expected = capture.layout;
    nlohmann::json sources = nlohmann::json::array();
    for (const auto id : textures)
        sources.push_back(
            {{"asset", id.str()}, {"source", path_utf8(catalog.records().at(id).source)}});
    ImportProcessRequest request{{{"recipe", sprite_atlas_source_recipe},
                                  {"revision", sprite_atlas_source_revision()},
                                  {"settings", options}},
                                 std::move(capture.images)};
    auto files =
        run_import_process(worker, project, std::move(request), sprite_atlas_source_limits(), stop);
    check_stop();
    const auto png = std::find_if(files.begin(), files.end(),
                                  [](const auto& file) { return file.name == "atlas.png"; });
    const auto layout = std::find_if(files.begin(), files.end(),
                                     [](const auto& file) { return file.name == "layout.json"; });
    if (files.size() != 2 || png == files.end() || layout == files.end() ||
        parse_bounded_json(layout->bytes, sprite_frame_value_byte_limit, 20000, 16) !=
            sprite_atlas_layout_document(expected))
        throw std::runtime_error("Atlas worker result does not match the captured input layout");
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    const auto& image = png->bytes;
    if (image.size() < 33 ||
        !std::equal(signature.begin(), signature.end(), image.begin(),
                    [](auto a, auto b) { return a == std::to_integer<unsigned char>(b); }))
        throw std::runtime_error("Atlas worker did not return a bounded PNG source");
    const auto word = [&](std::size_t offset) {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value = (value << 8) | std::to_integer<unsigned char>(image[offset + i]);
        return value;
    };
    if (word(8) != 13 || word(12) != 0x49484452 || word(16) != expected.width ||
        word(20) != expected.height || image[24] != std::byte{8} || image[25] != std::byte{6})
        throw std::runtime_error("Atlas PNG header disagrees with the reviewed dimensions/format");
    result.png = std::move(png->bytes);
    result.description = sprite_atlas_layout_document(expected);
    result.description["kind"] = "sprite_atlas";
    result.description["settings"] = options;
    result.description["inputs"] = std::move(sources);
    return result;
}
} // namespace forge::asset_detail
