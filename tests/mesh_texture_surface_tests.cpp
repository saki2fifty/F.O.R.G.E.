#ifdef FORGE_TEST_ASSET_AUTHORING
#include "fixtures/mesh_texture_paint_gallery.hpp"
#endif
#include "mesh_texture_surface.hpp"
#include "paint_texture_source.hpp"
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid surface mapping was admitted");
}
} // namespace
int main(int argc, char** argv) {
    try {
#ifdef FORGE_TEST_ASSET_AUTHORING
        if (argc == 4 && std::string_view(argv[1]) == "--author") {
            std::cout << test::author_mesh_texture_paint_gallery(std::filesystem::absolute(argv[2]),
                                                                 std::filesystem::absolute(argv[3]))
                             .str()
                      << '\n';
            return 0;
        }
#endif
        check(argc == 1, "Unexpected Mesh surface test arguments");
        MeshPart part;
        part.vertices = 4;
        part.indices = {0, 1, 2, 2, 1, 3};
        part.streams = {{"TEXCOORD_0", 2, std::vector<float>{0, 0, 1, 0, 0, 1, 1, 1}}};
        MaterialResourceData material;
        material.values.model = "forge.gltf.unlit.v1";
        material.values.double_sided = true;
        auto& slot = material.values.textures["baseColorTexture"];
        slot.semantic = TextureSemantic::Color;
        MeshTextureSurfaceTarget target{{AssetId::generate()}};
        material.textures[target.slot] = target.texture;
        const auto sample = [&](std::size_t primitive, Double3 barycentric) {
            MeshPickBudget budget;
            return mesh_texture_surface_point(part, material, target, {primitive, barycentric},
                                              budget);
        };
        auto a = sample(0, {.5, .25, .25});
        auto b = sample(1, {.25, .25, .5});
        check(a.uv == Double2{.25, .25} && b.uv == Double2{.75, .75} &&
                  mesh_texture_surface_connected(a, b),
              "Adjacent UV triangles lost image coordinates or continuous paths");
        auto seam = b;
        seam.indices = {4, 5, 6};
        check(!mesh_texture_surface_connected(a, seam), "A split UV edge connected a paint path");
        auto detached = b;
        detached.indices = {2, 4, 3};
        check(!mesh_texture_surface_connected(a, detached),
              "One shared vertex connected separate islands");
        slot.scale = {-1, -1};
        slot.offset = {1, 1};
        check(sample(0, {.5, .25, .25}).uv == Double2{.75, .75},
              "Complete Material UV flips differ from painting");
        slot.scale = {1, 1};
        slot.offset = {0, 0};
        validate_render_material(material);
        check(sample(0, {1, 0, 0}).uv == Double2{0, 0}, "Color mapping differs");
        auto data_material = material;
        data_material.values.model = "forge.gltf.metallic-roughness.v1";
        auto data_slot = slot;
        data_slot.semantic = TextureSemantic::Data;
        data_material.values.textures = {{"metallicRoughnessTexture", data_slot}};
        data_material.textures = {{"metallicRoughnessTexture", target.texture}};
        validate_render_material(data_material);
        MeshPickBudget data_budget;
        check(mesh_texture_surface_point(
                  part, data_material,
                  {target.texture, TextureSemantic::Data, "metallicRoughnessTexture"},
                  {0, {.5, .25, .25}}, data_budget)
                      .uv == Double2{.25, .25},
              "Admitted PBR Data image mapping differs from Color geometry");
        rejects([&] { (void)sample(0, {.5, .5, .5}); });
        rejects([&] { (void)sample(0, {-.1, .6, .5}); });
        rejects([&] { (void)sample(0, {std::numeric_limits<double>::quiet_NaN(), 0, 0}); });
        rejects([&] { (void)sample(2, {1, 0, 0}); });
        slot.rotation = .5f;
        rejects([&] { (void)sample(0, {1, 0, 0}); });
        slot.rotation = 0;
        slot.uv_set = 1;
        rejects([&] { (void)sample(0, {1, 0, 0}); });
        slot.uv_set = 0;
        slot.scale[0] = 2;
        rejects([&] { (void)sample(0, {1, 0, 0}); });
        slot.scale[0] = 1;
        material.values.double_sided = false;
        rejects([&] { (void)sample(0, {1, 0, 0}); });
        material.values.double_sided = true;
        target.texture.id = AssetId::generate();
        rejects([&] { (void)sample(0, {1, 0, 0}); });
        target.texture = material.textures.at(target.slot);
        auto& uv = std::get<std::vector<float>>(part.streams[0].values);
        uv[2] = 2;
        rejects([&] { (void)sample(0, {1, 0, 0}); });
        uv[2] = 1;
        uv[4] = uv[5] = 0;
        rejects([&] { (void)sample(0, {1, 0, 0}); });
        uv[5] = 1;
        MeshPickBudget exhausted{2};
        rejects([&] {
            (void)mesh_texture_surface_point(part, material, target, {0, {1, 0, 0}}, exhausted);
        });
        const auto canvas = PaintTextureValue::create(target.texture.id, 32, 32);
        PaintBrush brush;
        brush.color = {1, 0, 0, 1};
        brush.radius = .5;
        brush.hardness = 1;
        const std::array<PaintPoint, 2> points{
            {{a.uv[0] * 32, a.uv[1] * 32}, {b.uv[0] * 32, b.uv[1] * 32, true}}};
        const auto painted = canvas.paint(points, brush);
        check(painted.pixel(8, 8)[0] > 0 && painted.pixel(24, 24)[0] > 0 &&
                  painted.pixel(16, 16) == PaintPixel{},
              "Projected samples bridged a disconnected UV path");
        std::cout
            << "Mesh texture coordinates, Material usage, seams, validation and pixel brush PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
