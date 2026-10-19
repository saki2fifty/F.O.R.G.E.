#pragma once
#include "engine_render_resource.hpp"
#include "sprite_draw_selection.hpp"

namespace forge::test_sprite_resources {
template <class T>
ResourceLease<T> load(ResourcePool<T>& pool, typename ResourceTraits<T>::Data source,
                      AssetId id = AssetId::generate()) {
    const auto ticket =
        pool.request(AssetRef<T>{id}, std::string(64, 'a'), 1,
                     [source = std::move(source)](std::stop_token) mutable {
                         auto data =
                             std::make_unique<typename ResourceTraits<T>::Data>(std::move(source));
                         const auto bytes = data->resident_bytes();
                         return ResourceCandidate<T>{std::move(data), {bytes}};
                     });
    if (!pool.wait(ticket, std::chrono::seconds(5)))
        throw std::runtime_error("Sprite fixture resource failed to load");
    return pool.acquire(ticket);
}
inline TextureData image() {
    TextureData source;
    source.width = 8;
    source.height = 4;
    source.alpha = TextureAlpha::Straight;
    source.subresources = {std::vector<std::byte>(8 * 4 * 4, std::byte{255})};
    return source;
}
struct Fixture {
    ResourcePool<MeshAsset> meshes;
    ResourcePool<MaterialAsset> materials;
    ResourcePool<TextureAsset> textures;
    asset_detail::PreparedModelDraw draw;
    explicit Fixture(TextureData source = image(), MaterialAlpha alpha = MaterialAlpha::Blend) {
        validate_texture(source);
        draw.mesh = load(meshes, asset_detail::engine_mesh_resource(engine_primitive(8)),
                         engine_primitive(8).id);
        const auto texture = load(textures, std::move(source));
        MaterialResourceData material;
        material.values.model = "forge.gltf.unlit.v1";
        material.values.alpha = alpha;
        material.values.double_sided = true;
        material.values.depth_test = material.values.depth_write = false;
        material.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        material.values.textures["baseColorTexture"].sampler.min = TextureFilter::Nearest;
        material.values.textures["baseColorTexture"].sampler.mag = TextureFilter::Nearest;
        material.textures["baseColorTexture"] = {texture.identity().asset};
        const auto surface = load(materials, std::move(material));
        draw.selection = select_mesh_materials(
            draw.mesh.get(),
            std::vector<MaterialSlotOverride>{{"surface", {surface.identity().asset}}});
        draw.materials.emplace(surface.identity().asset, surface);
        draw.textures.emplace(
            asset_detail::DrawTextureKey{texture.identity().asset, TextureSemantic::Color},
            texture);
    }
};
} // namespace forge::test_sprite_resources
