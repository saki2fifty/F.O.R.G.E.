#pragma once
#include "display_resolve.hpp"
#include "mesh_draw.hpp"
#include "texture_gpu.hpp"
#include <cmath>
void check_transmission_render(forge::DiligentPresentation& presentation,
                               Diligent::IDeviceContext* context,
                               const std::filesystem::path& images) {
    using namespace forge;
    using namespace Diligent;
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, 0, -1, 1, 0, 1, 1, 0, 1, -1, 0}},
                    {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
                    {"TEXCOORD_0", 2, std::vector<float>{0, 1, 0, 0, 1, 0, 1, 1}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    const auto native = upload_mesh(presentation.device(), mesh);
    TextureDesc desc;
    desc.Name = "FORGE transmission rendering acceptance";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    presentation.device()->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation.device()->CreateTexture(desc, nullptr, &depth);
    require(color && depth, "Transmission acceptance targets unavailable");
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    Camera lens;
    lens.projection = std::uint32_t(CameraProjection::Orthographic);
    lens.orthographic_height = 2;
    lens.near_plane = .1;
    lens.far_plane = 10;
    AffineTransform origin;
    origin.m[3] = 1e12;
    auto view = camera_view(lens, origin, 32, 32);
    AffineTransform world = origin;
    world.m[11] = 2;
    TransmissionBackground background(presentation);
    DisplayResolve display(presentation);
    MaterialData glass;
    glass.model = "forge.gltf.metallic-roughness.v1";
    glass.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    glass.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {0}};
    glass.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {1}};
    glass.parameters["ior"] = {MaterialParameterType::Scalar, {1}};
    float white[]{1, 1, 1, 1};
    auto render = [&](const MaterialData& material, const MeshDraw::Textures& textures = {},
                      ITexture* source = nullptr) {
        MeshDraw draw(presentation, context, native.lods[0].parts[0], material, textures,
                      TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, white, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const auto snapshot =
            background.capture(context, source ? source : color.RawPtr(), view.viewport);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport area{0, 0, 32, 32, 0, 1};
        context->SetViewports(1, &area, 32, 32);
        draw.draw(context, world, view, {}, nullptr, nullptr, nullptr, {}, &snapshot);
        return readback(
            presentation.device(), context,
            display.resolve(context, color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0));
    };
    const auto clear = render(glass);
    const auto center = 16 * 32 + 16;
    require(clear[center][0] > 225 && clear[center][1] > 225 && clear[center][2] > 225,
            "Thin dielectric failed to transmit the opaque HDR background");
    save(clear, 32, 32, images / "transmission-thin.ppm");
    glass.parameters["thicknessFactor"] = {MaterialParameterType::Scalar, {1}};
    glass.parameters["attenuationColor"] = {MaterialParameterType::LinearColor3, {.25f, .5f, 1}};
    glass.parameters["attenuationDistance"] = {MaterialParameterType::Scalar, {1}};
    const auto absorbed = render(glass);
    require(absorbed[center][0] < absorbed[center][1] &&
                absorbed[center][1] < absorbed[center][2] &&
                absorbed[center][0] < clear[center][0] - 30,
            "Volume distance/color did not attenuate transmitted channels");
    save(absorbed, 32, 32, images / "transmission-absorption.ppm");
    world.m[0] = -1;
    require(render(glass) == absorbed,
            "Reflection changed optical thickness or removed the surface");
    world.m[0] = 1;
    world.m[10] = 2;
    const auto thicker = render(glass);
    require(thicker[center][0] < absorbed[center][0] && thicker[center][1] < absorbed[center][1],
            "World normal scale did not increase absorption distance");
    world.m[10] = 0;
    require(render(glass) == clear, "Rank-two surviving surface retained nonzero volume thickness");
    world.m[10] = 1;
    glass.parameters["attenuationColor"].value = {0, 1, 1, 0};
    // Keep endpoint evidence below PBRNeutral's highlight compression, which
    // deliberately desaturates cyan at unit intensity. 0.5 linear -> 188 sRGB.
    white[0] = white[1] = white[2] = .5f;
    const auto endpoints = render(glass);
    save(endpoints, 32, 32, images / "transmission-beer-endpoints.ppm");
    require(endpoints[center][0] < 2 && std::abs(int(endpoints[center][1]) - 188) <= 1 &&
                std::abs(int(endpoints[center][2]) - 188) <= 1,
            "Zero/one Beer attenuation endpoints generated invalid shading");
    white[0] = white[1] = white[2] = 1;
    glass.parameters.erase("attenuationDistance");
    require(render(glass) == clear, "Omitted attenuation distance did not mean no absorption");
    {
        // Exercise the exit interface seen from inside a volume. KHR volume
        // boundaries ignore doubleSided; a positive thickness must not cull it.
        const auto outside = view;
        AffineTransform inside = origin;
        inside.m[0] = inside.m[10] = -1;
        inside.m[11] = 4;
        view = camera_view(lens, inside, 32, 32);
        auto exit = glass;
        exit.double_sided = false;
        exit.parameters["attenuationColor"].value = {.25f, .5f, 1};
        exit.parameters["attenuationDistance"] = {MaterialParameterType::Scalar, {1}};
        const auto from_inside = render(exit);
        require(from_inside[center][0] < from_inside[center][1] &&
                    from_inside[center][1] < from_inside[center][2] &&
                    from_inside[center][0] < clear[center][0] - 30,
                "Volume exit interface was culled or lost its absorption");
        save(from_inside, 32, 32, images / "transmission-inside-exit.ppm");
        exit.double_sided = true;
        require(render(exit) == from_inside, "doubleSided changed a volume exit interface");
        world.m[0] = -1;
        require(render(exit) == from_inside, "Mirroring changed volume exit transport");
        world.m[0] = 1;
        // Incidence 60 degrees exceeds asin(1/1.5). No background transmission
        // may survive total internal reflection. There are no reflection lights
        // or environment in this fixture, so the interface is black.
        const double sine = std::sqrt(.75);
        inside.m = {-.5, 0, -sine, origin.m[3] + 2 * sine, 0, 1, 0, 0, sine, 0, -.5, 3};
        view = camera_view(lens, inside, 32, 32);
        exit.parameters["ior"].value[0] = 1.5f;
        const auto reflected = render(exit);
        require(reflected[center][0] < 5 && reflected[center][1] < 5 && reflected[center][2] < 5,
                "Total internal reflection leaked opaque-background transmission");
        save(reflected, 32, 32, images / "transmission-inside-total-reflection.ppm");
        view = outside;
    }
    // Alternating opaque bands distinguish spatial refraction/roughness from a
    // color-factor-only implementation. No authored scene data is involved.
    std::vector<std::array<std::uint16_t, 4>> bands(32 * 32);
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 32; ++x) {
            const std::uint16_t value = (x / 4) % 2 ? 0x3c00 : 0;
            bands[y * 32 + x] = {value, value, value, 0x3c00};
        }
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Usage = USAGE_IMMUTABLE;
    TextureSubResData subresource{bands.data(), 32 * 8};
    Diligent::TextureData source_data{&subresource, 1};
    RefCntAutoPtr<ITexture> striped;
    presentation.device()->CreateTexture(desc, &source_data, &striped);
    require(bool(striped), "Optical background bands allocation failed");
    glass.parameters["attenuationColor"].value = {1, 1, 1, 0};
    glass.parameters["thicknessFactor"].value[0] = 0;
    glass.parameters["ior"].value[0] = 1.5f;
    const auto sharp = render(glass, {}, striped);
    glass.parameters["roughnessFactor"].value[0] = 1;
    const auto blurred = render(glass, {}, striped);
    auto contrast = [](const auto& pixels) {
        int lo = 255, hi = 0;
        for (unsigned x = 4; x < 28; ++x) {
            lo = std::min(lo, int(pixels[16 * 32 + x][0]));
            hi = std::max(hi, int(pixels[16 * 32 + x][0]));
        }
        return hi - lo;
    };
    require(contrast(sharp) > 100 && contrast(blurred) < 30,
            "Transmission roughness did not filter the opaque background mip chain");
    save(blurred, 32, 32, images / "transmission-roughness.ppm");
    glass.parameters["roughnessFactor"].value[0] = 0;
    glass.parameters["thicknessFactor"].value[0] = .5f;
    world.m[0] = world.m[10] = std::sqrt(.75);
    world.m[2] = .5;
    world.m[8] = -.5;
    const auto refracted = render(glass, {}, striped);
    glass.parameters["thicknessFactor"].value[0] = 0;
    const auto unshifted = render(glass, {}, striped);
    require(refracted != unshifted, "Volume refraction did not displace the background");
    glass.parameters["thicknessFactor"].value[0] = .5f;
    glass.parameters["dispersion"] = {MaterialParameterType::Scalar, {10}};
    const auto dispersed = render(glass, {}, striped);
    require(std::any_of(dispersed.begin(), dispersed.end(),
                        [](const auto& pixel) {
                            return std::abs(int(pixel[0]) - int(pixel[2])) > 20 && pixel[1] > 0;
                        }),
            "Dispersion did not separate RGB refraction paths");
    save(dispersed, 32, 32, images / "transmission-dispersion.ppm");
    // Texture channels follow the exact KHR contracts: transmission R, thickness G.
    forge::TextureData factors;
    factors.width = factors.height = 1;
    factors.semantic = TextureSemantic::Data;
    factors.format = TextureFormat::RGBA8;
    factors.subresources = {{std::byte{0}, std::byte{255}, std::byte{255}, std::byte{255}}};
    auto factor_texture = upload_texture(presentation.device(), factors);
    glass.textures["transmissionTexture"].semantic = TextureSemantic::Data;
    MeshDraw::Textures textures;
    textures["transmissionTexture"] = factor_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    const auto blocked = render(glass, textures);
    require(blocked[center][0] < 10 && blocked[center][1] < 10 && blocked[center][2] < 10,
            "Transmission map did not multiply its red channel");
    glass.textures.clear();
    glass.parameters["dispersion"].value[0] = 0;
    factors.subresources[0][0] = std::byte{255};
    factors.subresources[0][1] = std::byte{0};
    factor_texture = upload_texture(presentation.device(), factors);
    glass.textures["thicknessTexture"].semantic = TextureSemantic::Data;
    textures.clear();
    textures["thicknessTexture"] = factor_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    require(render(glass, textures, striped) == unshifted,
            "Thickness map did not multiply its green channel");
    {
        auto combined = glass;
        combined.textures.clear();
        combined.parameters["clearcoatFactor"] = {MaterialParameterType::Scalar, {.2f}};
        combined.parameters["iridescenceFactor"] = {MaterialParameterType::Scalar, {.2f}};
        combined.parameters["sheenColorFactor"] = {MaterialParameterType::LinearColor3,
                                                   {.1f, .1f, .1f}};
        combined.parameters["sheenRoughnessFactor"] = {MaterialParameterType::Scalar, {.5f}};
        combined.parameters["anisotropyStrength"] = {MaterialParameterType::Scalar, {.5f}};
        combined.parameters["dispersion"].value[0] = 1;
        const auto profile = prepare_pbr_material(combined);
        MeshDraw::Textures all_textures;
        unsigned count = 0;
        for (const auto& [role, declaration] : profile.layout.textures) {
            auto& slot = combined.textures[role];
            slot.semantic = declaration.semantic;
            slot.sampler.lod_bias = .01f * count++;
            forge::TextureData tex;
            tex.width = tex.height = 1;
            tex.semantic = slot.semantic;
            tex.format = slot.semantic == TextureSemantic::Color ? TextureFormat::RGBA8Srgb
                                                                 : TextureFormat::RGBA8;
            tex.subresources = {std::vector<std::byte>(4, std::byte{255})};
            if (slot.semantic == TextureSemantic::Normal)
                tex.subresources[0][0] = tex.subresources[0][1] = std::byte{128};
            auto image = upload_texture(presentation.device(), tex);
            all_textures[role] = image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }
        const auto all = render(combined, all_textures, striped);
        require(count == 17 && all[center] != std::array<unsigned char, 4>{255, 0, 255, 255},
                "Combined optical/reflection texture and sampler bindings failed");
        save(all, 32, 32, images / "transmission-all-material-layers.ppm");
    }
}
