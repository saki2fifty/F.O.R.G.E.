#pragma once
#include <forge/material_source.hpp>
#include <stdexcept>

namespace forge {
// A preset of the ordinary material source, not a separate asset or registry.
inline MaterialSource sprite_material_source(AssetId id, AssetRef<TextureAsset> image,
                                             TextureFilter filtering = TextureFilter::Linear) {
    if (!image.id || (filtering != TextureFilter::Nearest && filtering != TextureFilter::Linear))
        throw std::invalid_argument(
            "A sprite material needs an image and nearest/linear filtering");
    MaterialData values;
    values.model = "forge.gltf.unlit.v1";
    values.alpha = MaterialAlpha::Blend;
    values.double_sided = true;
    values.depth_write = false;
    auto& slot = values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = slot.sampler.mip = filtering;
    slot.sampler.u = slot.sampler.v = TextureWrap::ClampEdge;
    const auto encoded = material_values_document(values);
    auto source = MaterialSource::create(id);
    source.document["overrides"] = {
        {"model", values.model},
        {"alpha", unsigned(values.alpha)},
        {"double_sided", values.double_sided},
        {"depth_test", values.depth_test},
        {"depth_write", values.depth_write},
        {"textures",
         {{"baseColorTexture",
           {{"asset", image}, {"slot", encoded.at("textures").at("baseColorTexture")}}}}}};
    source.validate();
    return source;
}
} // namespace forge
