#include "asset_bytes.hpp"
#include "texture_import.hpp"
#include "texture_importer.hpp"
#include "texture_ktx.hpp"
#include <forge/texture_bundle.hpp>
namespace forge::asset_detail {
std::vector<ArtifactFile> execute_texture_recipe(const ImportProcessRequest& request,
                                                 std::stop_token stop) {
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    require(!stop.stop_requested(), "Texture worker cancelled");
    const auto& p = request.payload;
    const auto recipe = p.at("recipe").get<std::string>();
    const bool container = recipe == "forge.texture.container";
    require(container || recipe == "forge.texture.image", "Unsupported native texture recipe");
    require(p.at("revision").get<std::string>() == texture_recipe_revision(),
            "Texture worker/toolchain revision mismatch");
    require(request.inputs.size() == 1 && request.inputs[0].name == "source.bin" &&
                request.inputs[0].bytes.size() <= 256ull * 1024 * 1024,
            "Invalid texture source snapshot");
    const auto& source = request.inputs[0].bytes;
    require(content_digest(source) == p.at("source_digest").get<std::string>(),
            "Texture source snapshot digest changed");
    const auto settings =
        texture_settings(container).effective(p.at("settings").get<ImportSettingsDocument>());
    const auto kind = p.at("kind").get<std::string>();
    const auto backend = p.at("backend").get<std::string>();
    require(backend == "none" || backend == "d3d12", "Unsupported texture preparation backend");
    TextureLimits limits;
    // Leave room in the supervised per-file limit for the cooked metadata envelope.
    limits.bytes = 252ull * 1024 * 1024;
    auto prepare = [&](const std::string& usage) {
        TextureSemantic semantic = usage == "data"     ? TextureSemantic::Data
                                   : usage == "normal" ? TextureSemantic::Normal
                                   : usage == "hdr" || (usage == "auto" && kind == "hdr")
                                       ? TextureSemantic::HdrColor
                                       : TextureSemantic::Color;
        TextureData result;
        if (container) {
            if (kind == "dds")
                result = import_texture_dds(source, semantic, limits, stop);
            else if (kind == "ktx")
                result = import_texture_ktx1(source, semantic, limits, stop);
            else if (kind == "ktx2")
                result = import_texture_ktx2(source, semantic,
                                             backend == "d3d12" ? BasisTarget::DesktopBc
                                                                : BasisTarget::Rgba8,
                                             false, limits, stop);
            else
                throw std::runtime_error("Unsupported texture container kind");
            if (usage == "auto" && (texture_format_info(result.format).float_bits ||
                                    result.format == TextureFormat::BC6Unsigned ||
                                    result.format == TextureFormat::BC6Signed))
                result.semantic = TextureSemantic::HdrColor;
        } else {
            require(kind == "image" || kind == "hdr", "Invalid raster source kind");
            TextureImportSettings s;
            s.semantic = semantic;
            const auto transfer = settings.at("transfer").get<std::string>();
            s.srgb =
                semantic == TextureSemantic::Color && (transfer == "srgb" || transfer == "auto");
            s.generate_mips = settings.at("mips").get<bool>();
            s.flip_vertical = settings.at("flip_vertical").get<bool>();
            s.flip_normal_green =
                semantic == TextureSemantic::Normal && settings.at("flip_green").get<bool>();
            s.premultiply_alpha =
                (semantic == TextureSemantic::Color || semantic == TextureSemantic::HdrColor) &&
                settings.at("premultiply").get<bool>();
            s.max_size = settings.at("max_size").get<unsigned>();
            const auto compression = settings.at("compression").get<std::string>();
            s.compression = compression == "bc" ? TextureCompression::NativeBc
                            : compression == "bc-high-quality"
                                ? TextureCompression::NativeBcHighQuality
                                : TextureCompression::None;
            const auto ext = p.at("extension").get<std::string>();
            require(ext.size() <= 8 && ext.starts_with('.') &&
                        ext.find_first_of("/\\:") == std::string::npos,
                    "Invalid image source extension hint");
            result = import_texture_image(source, "source" + ext, s, limits, stop);
        }
        const auto wrap = settings.at("wrap").get<std::string>();
        result.sampler.u = result.sampler.v = result.sampler.w =
            wrap == "repeat"   ? TextureWrap::Repeat
            : wrap == "mirror" ? TextureWrap::MirroredRepeat
                               : TextureWrap::ClampEdge;
        result.sampler.min = result.sampler.mag = result.sampler.mip =
            settings.at("filter") == "nearest" ? TextureFilter::Nearest : TextureFilter::Linear;
        result.sampler.anisotropy = settings.at("anisotropy").get<unsigned>();
        return result;
    };
    TextureBundleIndex index;
    std::vector<ArtifactFile> files;
    std::uint64_t total = 0;
    auto append = [&](TextureData result) {
        require(!stop.stop_requested(), "Texture worker cancelled before cooking");
        require(result.byte_size() <= 504ull * 1024 * 1024 - total,
                "Texture variants exceed aggregate output budget");
        auto cooked = encode_texture(result, limits);
        require(cooked.size() <= 504ull * 1024 * 1024 - total,
                "Texture variant envelope exceeds output budget");
        total += cooked.size();
        auto name = texture_variant_file(result.semantic);
        index.variants.push_back({result.semantic, name, content_digest(cooked), cooked.size()});
        files.push_back({std::move(name), std::move(cooked)});
    };
    auto primary = prepare(settings.at("semantic").get<std::string>());
    index.primary = primary.semantic;
    append(std::move(primary));
    for (const auto& usage : settings.at("additional_usages")) {
        const auto name = usage.get<std::string>();
        if (name == texture_variant_key(index.primary))
            continue;
        append(prepare(name));
    }
    require(!stop.stop_requested(), "Texture worker cancelled before bundle publication");
    files.push_back({"texture.json", encode_texture_bundle_index(index)});
    return files;
}
} // namespace forge::asset_detail
