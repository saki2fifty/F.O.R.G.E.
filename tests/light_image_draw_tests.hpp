#pragma once
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include "texture_gpu.hpp"
#include <forge/engine_texture_assets.hpp>

inline void test_light_image_draws(forge::DiligentPresentation& presentation,
                                   Diligent::IDeviceContext* context,
                                   const forge::GpuMeshPart& mesh, Diligent::ITexture* target,
                                   Diligent::ITextureView* depth,
                                   const forge::AffineTransform& world,
                                   const forge::CameraView& camera) {
    using namespace forge;
    using namespace Diligent;
    const auto check = test_bloom::check;
    auto* device = presentation.device();
    MaterialData material;
    material.model = "forge.gltf.metallic-roughness.v1";
    material.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    material.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.7f}};
    MeshDraw draw(presentation, context, mesh, material, {}, TEX_FORMAT_RGBA32_FLOAT,
                  TEX_FORMAT_D32_FLOAT);
    check(draw.supports_light_images(), "Current lit receiver lost its light image packet");
    auto* selected_draw = &draw;
    const auto pixels = [&](std::span<const LightView> lights,
                            std::span<const MeshDraw::LightImage> images = {}) {
        auto* color = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &color, depth, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]{};
        context->ClearRenderTarget(color, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(depth, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, 32, 32, 0, 1};
        context->SetViewports(1, &viewport, 32, 32);
        selected_draw->draw(context, world, camera, lights, nullptr, nullptr, nullptr, {}, nullptr,
                            {}, nullptr, {}, {}, {}, nullptr, nullptr, nullptr, {}, nullptr,
                            images);
        return test_bloom::read(device, context, target);
    };
    const auto source = [](bool cube, bool white, bool mips = false) {
        forge::TextureData data;
        data.dimension = cube ? TextureDimension::Cube : TextureDimension::D2;
        data.format = TextureFormat::RGBA8;
        data.semantic = TextureSemantic::Color;
        data.alpha = TextureAlpha::Straight;
        data.width = data.height = mips ? 8 : 1;
        data.mips = mips ? 4 : 1;
        data.sampler.min = data.sampler.mag = data.sampler.mip = TextureFilter::Nearest;
        for (unsigned face = 0; face < (cube ? 6u : 1u); ++face)
            for (unsigned mip = 0; mip < data.mips; ++mip) {
                const unsigned side = data.width >> mip;
                std::vector<std::byte> bytes(side * side * 4);
                const std::array<unsigned, 4> value =
                    white  ? std::array<unsigned, 4>{255, 255, 255, 17}
                    : mips ? (mip == 3 ? std::array<unsigned, 4>{255, 64, 32, 17}
                                       : std::array<unsigned, 4>{64 + 32 * mip, 128, 192, 17})
                           : std::array<unsigned, 4>{64 + 8 * face, 128 - 8 * face, 192 - 8 * face,
                                                     17};
                for (unsigned texel = 0; texel < side * side; ++texel)
                    for (unsigned channel = 0; channel < 4; ++channel)
                        bytes[texel * 4 + channel] = std::byte(value[channel]);
                data.subresources.push_back(std::move(bytes));
            }
        validate_texture(data);
        return data;
    };
    const auto compare = [&](const auto& actual, const auto& ordinary,
                             std::array<double, 3> multiplier) {
        unsigned lit = 0;
        for (unsigned i = 0; i < actual.size(); ++i)
            for (unsigned c = 0; c < 3; ++c) {
                const double expected = ordinary[i][c] * multiplier[c];
                check(std::isfinite(actual[i][c]) && std::abs(actual[i][c] - expected) <=
                                                         2e-5 * std::max(1., std::abs(expected)),
                      "Image RGB did not multiply actual direct-light radiance");
                lit += ordinary[i][c] > .001;
            }
        check(lit > 100, "Light image native oracle had no illuminated geometry");
    };
    Light light;
    light.image_width = light.image_height = 8;
    AffineTransform placement;
    placement.m[3] = camera.position[0];
    placement.m[7] = camera.position[1];
    placement.m[11] = camera.position[2];
    for (unsigned kind = 0; kind < 3; ++kind) {
        light.kind = kind;
        const auto value = light_view(light, placement);
        const auto frame = light_image_frame(light, placement);
        const auto ordinary = pixels(std::span(&value, 1));
        for (bool white : {false, true}) {
            const auto data = source(kind == unsigned(LightKind::Point), white);
            auto image = upload_texture(device, data);
            auto sampler = upload_sampler(device, data.sampler);
            const MeshDraw::LightImage selected{
                &frame,
                {image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), sampler},
                &data.sampler};
            const auto actual = pixels(std::span(&value, 1), std::span(&selected, 1));
            const unsigned face = kind == unsigned(LightKind::Point) ? 4 : 0;
            compare(actual, ordinary,
                    white ? std::array<double, 3>{1, 1, 1}
                          : std::array<double, 3>{(64. + 8 * face) / 255, (128. - 8 * face) / 255,
                                                  (192. - 8 * face) / 255});
            compare(pixels(std::span(&value, 1)), ordinary, {1, 1, 1});
            // SRBs for unused windings must release references before owner leases die.
            draw.clear_decals(context);
        }
    }
    light.kind = unsigned(LightKind::Directional);
    const auto value = light_view(light, placement);
    const auto ordinary = pixels(std::span(&value, 1));
    const auto data = source(false, false, true);
    auto image = upload_texture(device, data);
    auto sampler = upload_sampler(device, data.sampler);
    light.image_width = light.image_height = .001f;
    light.image_tiled = true;
    const auto frame = light_image_frame(light, placement);
    const MeshDraw::LightImage selected{
        &frame, {image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), sampler}, &data.sampler};
    compare(pixels(std::span(&value, 1), std::span(&selected, 1)), ordinary,
            {1, 64. / 255, 32. / 255});
    std::array<LightView, 2> two{value, value};
    std::array<MeshDraw::LightImage, 2> mixed{{{}, selected}};
    compare(pixels(two, mixed), ordinary, {2, 1 + 64. / 255, 1 + 32. / 255});
    draw.clear_decals(context);
    // Punctual images also compose inside the shared area-light shader variant.
    MeshDraw area_draw(presentation, context, mesh, material, {}, TEX_FORMAT_RGBA32_FLOAT,
                       TEX_FORMAT_D32_FLOAT, true, SHADER_COMPILER_DEFAULT,
                       SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr, MeshDrawProfile::Mesh, true);
    selected_draw = &area_draw;
    Light area;
    area.kind = unsigned(LightKind::Sphere);
    area.intensity = 2;
    area.emitter_radius = .25f;
    const auto area_value = light_view(area, placement);
    const auto area_pixels = pixels(std::span(&area_value, 1));
    std::array<LightView, 2> mixed_kinds{value, area_value};
    std::array<MeshDraw::LightImage, 2> mixed_images{{selected, {}}};
    auto expected = ordinary;
    for (unsigned i = 0; i < expected.size(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            expected[i][c] =
                float(ordinary[i][c] * std::array<double, 3>{1, 64. / 255, 32. / 255}[c] +
                      area_pixels[i][c]);
    compare(pixels(mixed_kinds, mixed_images), expected, {1, 1, 1});
    area_draw.clear_decals(context);
    selected_draw = &draw;

    // Cube light images must honor the source sampler's LOD interval too.
    auto cube_data = source(true, false, true);
    cube_data.sampler.min_lod = cube_data.sampler.max_lod = 3;
    auto cube_image = upload_texture(device, cube_data);
    auto cube_sampler = upload_sampler(device, cube_data.sampler);
    Light cube_light;
    cube_light.kind = unsigned(LightKind::Point);
    const auto cube_value = light_view(cube_light, placement);
    const auto cube_frame = light_image_frame(cube_light, placement);
    const MeshDraw::LightImage cube_selected{
        &cube_frame,
        {cube_image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), cube_sampler},
        &cube_data.sampler};
    const auto cube_ordinary = pixels(std::span(&cube_value, 1));
    compare(pixels(std::span(&cube_value, 1), std::span(&cube_selected, 1)), cube_ordinary,
            {1, 64. / 255, 32. / 255});
    draw.clear_decals(context);

    // Admission is shared with projected surfaces: eight distinct image/sampler
    // pairs, with actual native views rather than fabricated pointer tokens.
    std::array<RefCntAutoPtr<ITexture>, 8> maps;
    std::array<DecalMaterial, 8> materials;
    std::array<MeshDraw::Decal, 8> decals;
    DecalProjector projector;
    const auto projection = decal_view(projector, {});
    for (unsigned i = 0; i < 8; ++i) {
        maps[i] = upload_texture(device, source(false, true));
        materials[i].textures[0].asset = engine_texture(EngineTexture::White);
        materials[i].textures[0].slot.sampler = data.sampler;
        decals[i] = {&projection, &materials[i]};
        decals[i].textures[0] = {maps[i]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), sampler};
    }
    check(draw.accepts_projected(std::span(decals).first(7), std::span(&selected, 1)) &&
              !draw.accepts_projected(decals, std::span(&selected, 1)),
          "Light images did not share the projected map budget");
    auto shared = selected;
    shared.texture = decals[0].textures[0];
    check(draw.accepts_projected(decals, std::span(&shared, 1)),
          "Shared decal/light image and identical sampler consumed another slot");
    auto other = data.sampler;
    other.u = TextureWrap::ClampEdge;
    shared.intent = &other;
    check(!draw.accepts_projected(decals, std::span(&shared, 1)),
          "Different image sampler intent was silently deduplicated");
    Light point;
    point.kind = unsigned(LightKind::Point);
    const auto point_frame = light_image_frame(point, placement);
    shared.frame = &point_frame;
    check(!draw.accepts_projected({}, std::span(&shared, 1)),
          "Point image admitted a 2D view into a Cube binding");
}
