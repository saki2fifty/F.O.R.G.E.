#include "engine_render_resource.hpp"
#include <bit>
#include <cstring>
#include <forge/texture_bundle.hpp>
namespace forge::asset_detail {
namespace {
TextureSemantic texture_semantic(const EngineTextureAsset& asset,
                                 std::optional<TextureSemantic> requested) {
    return requested.value_or(asset.kind == EngineTexture::FlatNormal ? TextureSemantic::Normal
                                                                      : TextureSemantic::Color);
}
} // namespace
TextureData engine_texture_resource(AssetRef<TextureAsset> ref,
                                    std::optional<TextureSemantic> requested) {
    const auto* asset = engine_texture_asset(ref.id);
    if (!asset)
        throw std::runtime_error("Engine asset is not a texture");
    TextureData texture;
    texture.dimension = asset->dimension;
    texture.semantic = texture_semantic(*asset, requested);
    texture.format = texture.semantic == TextureSemantic::HdrColor ? TextureFormat::RGBA32Float
                     : texture.semantic == TextureSemantic::Color  ? TextureFormat::RGBA8Srgb
                                                                   : TextureFormat::RGBA8;
    texture.alpha = TextureAlpha::Opaque;
    const bool checker = asset->kind == EngineTexture::Checker;
    texture.width = texture.height = checker ? 4 : 1;
    texture.depth = texture.dimension == TextureDimension::D3 && checker ? 4 : 1;
    validate_texture_metadata(texture);
    const unsigned faces = texture.dimension == TextureDimension::Cube ||
                                   texture.dimension == TextureDimension::CubeArray
                               ? 6
                               : 1;
    const auto layout = texture_layout(texture, 0);
    texture.subresources.resize(faces);
    for (auto& data : texture.subresources) {
        data.resize(layout.bytes);
        for (unsigned z = 0; z < texture.depth; ++z)
            for (unsigned y = 0; y < texture.height; ++y)
                for (unsigned x = 0; x < texture.width; ++x) {
                    std::array<float, 4> color{1, 1, 1, 1};
                    if (asset->kind == EngineTexture::Black)
                        color = {0, 0, 0, 1};
                    else if (asset->kind == EngineTexture::FlatNormal)
                        color = {.5f, .5f, 1, 1};
                    else if (checker)
                        color = ((x + y + z) & 1) ? std::array<float, 4>{0, 0, 0, 1}
                                                  : std::array<float, 4>{1, 0, 1, 1};
                    const auto pixel = (std::size_t(z) * texture.height + y) * texture.width + x;
                    if (texture.format == TextureFormat::RGBA32Float) {
                        static_assert(std::endian::native == std::endian::little);
                        std::memcpy(data.data() + pixel * sizeof(color), color.data(),
                                    sizeof(color));
                    } else
                        for (unsigned c = 0; c < 4; ++c)
                            data[pixel * 4 + c] =
                                std::byte(static_cast<unsigned>(color[c] * 255 + .5f));
                }
    }
    validate_texture(texture);
    return texture;
}
ResourceTicket request_engine_texture(ResourcePool<TextureAsset>& pool, AssetRef<TextureAsset> ref,
                                      std::optional<TextureSemantic> requested) {
    const auto* asset = engine_texture_asset(ref.id);
    if (!asset)
        throw std::runtime_error("Engine asset is not a texture");
    const auto semantic = texture_semantic(*asset, requested);
    return pool.request(
        ref, engine_asset_revision(ref.id), 1,
        [ref, semantic](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Engine texture preparation cancelled");
            auto value = std::make_unique<TextureData>(engine_texture_resource(ref, semantic));
            const auto bytes = value->resident_bytes();
            return ResourceCandidate<TextureAsset>{std::move(value), {bytes}};
        },
        {}, 0, std::string(texture_variant_key(semantic)));
}
} // namespace forge::asset_detail
