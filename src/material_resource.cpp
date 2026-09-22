#include "asset_bytes.hpp"
#include "pbr_material.hpp"
#include <forge/material_resource.hpp>
namespace forge {
void validate_render_material(const MaterialResourceData& data) {
    validate_material_bindings(data.values, data.textures);
    if (data.surface) {
        if (!data.surface->shader.id || !valid_content_digest(data.surface->revision) ||
            !data.surface->program.surface)
            throw std::runtime_error("Material has an invalid cooked surface dependency");
        validate_shader(data.surface->program);
        validate_surface_material(data.values, *data.surface->program.surface);
    } else
        (void)prepare_pbr_material(data.values);
}
std::size_t MaterialResourceData::resident_bytes() const {
    std::size_t bytes = sizeof(*this) - sizeof(values) + values.resident_bytes();
    for (const auto& [name, ref] : textures) {
        (void)ref;
        bytes += sizeof(std::pair<const std::string, AssetRef<TextureAsset>>) + 4 * sizeof(void*) +
                 name.capacity();
    }
    if (surface)
        bytes += surface->revision.capacity() + surface->program.resident_bytes();
    return bytes;
}
ResourcePool<MaterialAsset>::Loader material_resource_loader(std::filesystem::path path,
                                                             std::string digest,
                                                             MaterialTextureBindings bindings,
                                                             MaterialLayout layout) {
    resource_detail::valid_revision(digest);
    return [path = std::move(path), digest = std::move(digest), bindings = std::move(bindings),
            layout = std::move(layout)](std::stop_token stop) {
        if (stop.stop_requested())
            throw std::runtime_error("Material load cancelled");
        const auto bytes = asset_detail::read_bytes(path, 1024 * 1024 + 24);
        if (asset_detail::content_digest(bytes) != digest)
            throw std::runtime_error("Cooked material digest mismatch");
        auto data = std::make_unique<MaterialResourceData>();
        data->values = decode_material(bytes);
        data->textures = bindings;
        validate_material_bindings(data->values, data->textures);
        validate_material_layout(data->values, layout);
        if (stop.stop_requested())
            throw std::runtime_error("Material load cancelled");
        const auto used = data->resident_bytes();
        return ResourceCandidate<MaterialAsset>{std::move(data), ResourceMemory{used}};
    };
}
} // namespace forge
