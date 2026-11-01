#include "sprite_frame_mip_tests.hpp"
#include "sprite_resource_fixture.hpp"
#include "surface_material_fixture.hpp"
#include <iostream>
#include <limits>

using namespace forge;
using namespace forge::asset_detail;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Incompatible Sprite profile accepted");
}
} // namespace
int main() {
    try {
        test_sprite_frame_mip_pixels();
        test_sprite_resources::Fixture fixture;
        const auto& original = fixture.draw;
        const auto mesh_key = prepared_model_draw_key(original, false);
        const auto sprite_key = prepared_model_draw_key(original, false, MeshDrawProfile::Sprite);
        check(mesh_key != sprite_key && mesh_key.resources == sprite_key.resources &&
                  mesh_key.bindings == sprite_key.bindings,
              "Sprite pipeline aliases Mesh or duplicates resource identity");
        const auto& texture = sprite_source_texture(original);
        check(texture.width == 8 && texture.height == 4 &&
                  &texture == &original.textures.begin()->second.get(),
              "Sprite source bypassed the existing texture lease");
        rejects([&] { sprite_source_texture(original, true); });
        auto changed = original;
        changed.texture_fallbacks[{AssetId::generate(), TextureSemantic::Color}] =
            original.textures.begin()->first;
        rejects([&] { sprite_source_texture(changed); });
        changed = original;
        changed.selection.unresolved.push_back("removed");
        rejects([&] { sprite_source_texture(changed); });
        changed = original;
        changed.selection.parts[0].clear();
        rejects([&] { sprite_source_texture(changed); });
        changed = original;
        changed.textures.clear();
        rejects([&] { sprite_source_texture(changed); });
        changed = original;
        changed.mesh = test_sprite_resources::load(
            fixture.meshes, engine_mesh_resource(engine_primitive(0)), engine_primitive(0).id);
        rejects([&] { sprite_source_texture(changed); });

        const auto material = original.materials.begin()->second->values;
        validate_sprite_material_profile(material);
        {
            // CPU-only programs test selection/leases, never native realization.
            SurfaceShaderDefinition definition;
            definition.uv_sets = {0};
            definition.sprite_image = "image";
            definition.textures["image"] = material.textures.at("baseColorTexture");
            definition.textures["detail"] = definition.textures.at("image");
            definition.labels["image"] = "Atlas image";
            MaterialResourceData custom;
            custom.values = surface_material_defaults(definition);
            custom.values.alpha = MaterialAlpha::Blend;
            custom.values.depth_write = false;
            const auto image_ref =
                original.materials.begin()->second->textures.at("baseColorTexture");
            custom.textures = {{"detail", image_ref}, {"image", image_ref}};
            custom.surface = MaterialShaderSnapshot{{AssetId::generate()},
                                                    std::string(64, 'b'),
                                                    test::surface_fixture_program(definition)};
            check(sprite_material_image(custom) == "image",
                  "Sprite image was inferred from order or display label");
            auto candidate = original;
            const auto id = candidate.selection.parts[0][0].id;
            ResourcePool<MaterialAsset> custom_materials;
            candidate.materials[id] = test_sprite_resources::load(custom_materials, custom, id);
            check(&sprite_source_texture(candidate) == &texture &&
                      sprite_source_texture_lease(candidate).identity() ==
                          sprite_source_texture_lease(original).identity(),
                  "Custom Sprite image bypassed the exact existing Texture lease");
            auto incompatible = custom;
            incompatible.values.textures.at("image").offset[0] = .5f;
            rejects([&] { sprite_material_image(incompatible); });
            incompatible = custom;
            auto mesh_definition = definition;
            mesh_definition.sprite_image.reset();
            mesh_definition.labels.clear();
            incompatible.surface->program = test::surface_fixture_program(mesh_definition);
            rejects([&] { sprite_material_image(incompatible); });
            incompatible = custom;
            incompatible.surface->program.stages.pop_back();
            rejects([&] { sprite_material_image(incompatible); });
            candidate.textures.clear();
            rejects([&] { sprite_source_texture(candidate); });
        }
        std::vector<SpriteFrameRegion> frame_bank{{0, 0, 2, 1}, {2, 0, 1, 2}};
        frame_bank.back().rotated_clockwise = true;
        // Storage orientation alone does not change alpha/depth admission.
        for (auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Mask, MaterialAlpha::Blend}) {
            auto value = material;
            value.alpha = alpha;
            value.depth_write = true;
            validate_sprite_frame_material(value, frame_bank);
        }
        frame_bank.back().source_width = 4;
        frame_bank.back().trim_x = 1;
        validate_sprite_frame_material(material, frame_bank);
        for (auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Mask}) {
            auto value = material;
            value.alpha = alpha;
            rejects([&] { validate_sprite_frame_material(value, frame_bank); });
        }
        auto depth_trim = material;
        depth_trim.depth_write = true;
        rejects([&] { validate_sprite_frame_material(depth_trim, frame_bank); });
        auto incompatible = [&](auto change) {
            auto value = material;
            change(value);
            rejects([&] { validate_sprite_material_profile(value); });
        };
        incompatible([](auto& m) { m.model = "forge.gltf.metallic-roughness.v1"; });
        incompatible([](auto& m) { m.textures.clear(); });
        incompatible([](auto& m) { m.textures["normalTexture"] = {}; });
        auto slot_incompatible = [&](auto change) {
            incompatible([&](auto& m) { change(m.textures.at("baseColorTexture")); });
        };
        slot_incompatible([](auto& s) { s.semantic = TextureSemantic::Data; });
        slot_incompatible([](auto& s) { s.dimension = TextureDimension::Cube; });
        slot_incompatible([](auto& s) { s.uv_set = 1; });
        slot_incompatible([](auto& s) { s.offset[0] = .1f; });
        slot_incompatible([](auto& s) { s.scale[1] = -1; });
        slot_incompatible([](auto& s) { s.rotation = .1f; });
        slot_incompatible([](auto& s) { s.sampler.anisotropy = 2; });
        slot_incompatible([](auto& s) { s.sampler.mag = TextureFilter::Linear; });
        slot_incompatible([](auto& s) { s.sampler.lod_bias = 1; });
        slot_incompatible([](auto& s) { s.sampler.min_lod = 1; });
        slot_incompatible([](auto& s) { s.sampler.max_lod = -1; });
        slot_incompatible([](auto& s) { s.sampler.compare = TextureCompare::Less; });
        slot_incompatible(
            [](auto& s) { s.sampler.max_lod = std::numeric_limits<float>::quiet_NaN(); });
        for (const auto format : {TextureFormat::RGBA8, TextureFormat::RGBA8Srgb})
            for (const auto alpha : {TextureAlpha::Opaque, TextureAlpha::Straight}) {
                auto value = texture;
                value.format = format;
                value.alpha = alpha;
                validate_sprite_texture_profile(value);
            }
        for (const auto alpha :
             {TextureAlpha::Premultiplied, TextureAlpha::Unknown, TextureAlpha::Custom}) {
            auto value = texture;
            value.alpha = alpha;
            rejects([&] { validate_sprite_texture_profile(value); });
        }
        for (const auto format :
             {TextureFormat::BC3, TextureFormat::RGBA16Float, TextureFormat::R8}) {
            auto value = texture;
            value.format = format;
            rejects([&] { validate_sprite_texture_profile(value); });
        }
        auto cube = texture;
        cube.dimension = TextureDimension::Cube;
        rejects([&] { validate_sprite_texture_profile(cube); });
        auto data = texture;
        data.semantic = TextureSemantic::Data;
        rejects([&] { validate_sprite_texture_profile(data); });
        check(prepared_model_draw_key(original, false) == mesh_key &&
                  prepared_model_draw_key(original, false, MeshDrawProfile::Sprite) == sprite_key,
              "Failed Sprite selections changed the retained resource selection");
        std::cout << "Sprite material/texture admission, exact lease ownership and distinct "
                     "pipeline keys passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
