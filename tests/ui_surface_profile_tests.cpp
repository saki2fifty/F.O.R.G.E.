#include "ui_surface_profile.hpp"
#include <cmath>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        const AssetRef<RenderTextureAsset> output{AssetId::generate()};
        MaterialResourceData source;
        source.values.model = "forge.gltf.unlit.v1";
        source.values.double_sided = true;
        source.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        source.render_textures["baseColorTexture"] = output;
        for (unsigned flips = 0; flips < 4; ++flips) {
            auto material = source;
            auto& slot = material.values.textures.at("baseColorTexture");
            for (unsigned axis = 0; axis < 2; ++axis)
                if (flips & (1 << axis)) {
                    slot.scale[axis] = -1;
                    slot.offset[axis] = 1;
                }
            const auto mapping = ui_surface_material_mapping(material, output);
            // Includes captured drags outside the image, without sampler wrap.
            for (const Double2 local : {Double2{-.25, .25}, Double2{-2, 3}}) {
                const auto image = mapping.image(local);
                for (unsigned axis = 0; axis < 2; ++axis) {
                    const double expected =
                        flips & (1 << axis) ? .5 - local[axis] : .5 + local[axis];
                    check(image[axis] == expected, "Image axis differs from canonical UV mapping");
                }
                check(mapping.local(image) == local, "Image mapping lost its local coordinates");
            }
        }
        const auto reject = [&](auto change) {
            auto material = source;
            change(material);
            bool rejected = false;
            try {
                (void)ui_surface_material_mapping(material, output);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            check(rejected, "Unsupported UI surface Material admitted input");
        };
        reject([](auto& m) { m.values.alpha = MaterialAlpha::Blend; });
        reject([](auto& m) { m.values.alpha = MaterialAlpha::Mask; });
        reject([](auto& m) { m.values.double_sided = false; });
        reject([](auto& m) { m.values.depth_test = false; });
        reject([](auto& m) { m.values.depth_write = false; });
        reject([](auto& m) { m.values.model = "forge.gltf.metallic-roughness.v1"; });
        reject([](auto& m) { m.surface.emplace(); });
        reject([](auto& m) { m.values.textures.at("baseColorTexture").uv_set = 1; });
        reject([](auto& m) {
            m.values.textures.at("baseColorTexture").semantic = TextureSemantic::Data;
        });
        reject([](auto& m) { m.values.textures.at("baseColorTexture").rotation = .5; });
        reject([](auto& m) { m.values.textures.at("baseColorTexture").offset[0] = .25; });
        reject([](auto& m) { m.values.textures.at("baseColorTexture").scale[1] = 2; });
        reject([](auto& m) { m.values.textures.at("baseColorTexture").scale[1] = NAN; });
        reject([](auto& m) { m.values.textures["other"] = {}; });
        reject([](auto& m) { m.textures["baseColorTexture"] = {AssetId::generate()}; });
        reject([](auto& m) { m.render_textures["other"] = {AssetId::generate()}; });
        reject([](auto& m) { m.render_textures["baseColorTexture"] = {AssetId::generate()}; });
        reject([](auto& m) { m.render_textures.clear(); });
        std::cout << "UI surface image axes, inverse/captured coordinates and Material rejection "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
