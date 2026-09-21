#pragma once
#include "mesh_draw.hpp"
#include "texture_gpu.hpp"
void check_morph_render(forge::DiligentPresentation& presentation,
                        Diligent::IDeviceContext* context, const std::filesystem::path& images) {
    using namespace forge;
    using namespace Diligent;
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {
        {"POSITION", 3, std::vector<float>{-.5f, -.5f, 0, -.5f, .5f, 0, .5f, .5f, 0, .5f, -.5f, 0}},
        {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
        {"TANGENT", 4, std::vector<float>{1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1}},
        {"COLOR_0", 4, std::vector<float>(16, 1)},
        {"TEXCOORD_19", 2, std::vector<float>{.25f, .5f, .25f, .5f, .25f, .5f, .25f, .5f}}};
    auto target = [&](const char* semantic, std::initializer_list<float> delta) {
        std::vector<float> values;
        for (unsigned i = 0; i < 4; ++i)
            values.insert(values.end(), delta);
        part.morph_targets.push_back({{semantic, unsigned(delta.size()), std::move(values)}});
    };
    target("POSITION", {.5f, 0, 0});
    target("COLOR_0", {-2, -1, 0, 0});
    target("TEXCOORD_19", {.5f, 0});
    target("NORMAL", {0, 0, 2});
    target("TANGENT", {-1, 1, 0});
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.morph_names = {"Move", "Color", "UV", "Normal", "Tangent"};
    mesh.morph_defaults = {1, 0, 0, 0, 0};
    mesh.lods = {{1, {part}}};
    const auto gpu = upload_mesh(presentation.device(), mesh);
    require(gpu.lods[0].parts[0].bounds.maximum[0] >= 1, "GPU draw retained unmorphed bounds");
    MaterialData material;
    material.model = "forge.gltf.unlit.v1";
    MeshDraw draw(presentation, context, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_RGBA8_UNORM,
                  TEX_FORMAT_D32_FLOAT);
    TextureDesc desc;
    desc.Name = "FORGE morph channel acceptance";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    presentation.device()->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation.device()->CreateTexture(desc, nullptr, &depth);
    require(color && depth, "Morph draw target allocation failed");
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    Camera lens;
    lens.projection = std::uint32_t(CameraProjection::Orthographic);
    lens.orthographic_height = 4;
    const auto view = camera_view(lens, AffineTransform{}, 32, 32);
    AffineTransform world;
    world.m[11] = 2;
    std::string stage = "rest weights";
    auto render = [&](MeshDraw& selected, std::span<const float> weights,
                      std::span<const LightView> lights = std::span<const LightView>{}) {
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport area{0, 0, 32, 32, 0, 1};
        context->SetViewports(1, &area, 32, 32);
        selected.draw(context, world, view, lights, nullptr, nullptr, nullptr, {}, nullptr,
                      weights);
        try {
            return readback(presentation.device(), context, rtv);
        } catch (const std::exception& error) {
            throw std::runtime_error("Morph " + stage + ": " + error.what());
        }
    };
    std::array<float, 5> weights{};
    const auto rest = render(draw, weights);
    save(rest, 32, 32, images / "morph-rest.ppm");
    auto center_x = [](const auto& image) {
        double total = 0, count = 0;
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x)
                if (image[y * 32 + x][0] > 0) {
                    total += x;
                    ++count;
                }
        require(count > 0, "Morphed geometry disappeared");
        return total / count;
    };
    stage = "default weights";
    const auto moved = render(draw, {});
    require(center_x(moved) > center_x(rest) + 3, "Default morph weight did not move geometry");
    weights[0] = -1;
    stage = "negative weights";
    const auto negative = render(draw, weights);
    require(center_x(negative) < center_x(rest) - 3,
            "Negative morph weight was clamped or ignored");
    save(moved, 32, 32, images / "morph-positive.ppm");
    save(negative, 32, 32, images / "morph-negative.ppm");
    weights = {0, 1, 0, 0, 0};
    stage = "color weights";
    const auto colored = render(draw, weights);
    require(colored[16 * 32 + 16] == std::array<unsigned char, 4>{0, 0, 255, 255},
            "Morphed color was not added then clamped");
    weights = {};
    forge::TextureData texture;
    texture.width = 2;
    texture.height = 1;
    texture.format = TextureFormat::RGBA8Srgb;
    texture.subresources = {{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
                             std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}}};
    auto image = upload_texture(presentation.device(), texture);
    material.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    material.textures["baseColorTexture"].uv_set = 19;
    material.textures["baseColorTexture"].sampler.min = TextureFilter::Nearest;
    material.textures["baseColorTexture"].sampler.mag = TextureFilter::Nearest;
    MeshDraw::Textures textures;
    textures["baseColorTexture"] = image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    MeshDraw textured(presentation, context, gpu.lods[0].parts[0], material, textures,
                      TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    stage = "UV rest";
    const auto red = render(textured, weights);
    weights[2] = 1;
    stage = "UV changed";
    const auto blue = render(textured, weights);
    require(red[16 * 32 + 16][0] > 240 && blue[16 * 32 + 16][2] > 240 && blue[16 * 32 + 16][0] < 10,
            "Morph UV19 did not route through material sampling");
    material.textures.clear();
    material.model = "forge.gltf.metallic-roughness.v1";
    material.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    MeshDraw lit(presentation, context, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_RGBA8_UNORM,
                 TEX_FORMAT_D32_FLOAT);
    Light light;
    light.intensity = 2;
    const auto illumination = light_view(light, AffineTransform{});
    weights = {};
    stage = "normal rest";
    const auto bright = render(lit, weights, std::span(&illumination, 1));
    weights[3] = 1;
    stage = "normal changed";
    const auto dark = render(lit, weights, std::span(&illumination, 1));
    require(bright[16 * 32 + 16][0] > 50 && dark[16 * 32 + 16][0] < 10,
            "Morph normal did not reach native lighting");
    texture.width = texture.height = 1;
    texture.format = TextureFormat::RGBA8;
    texture.semantic = TextureSemantic::Normal;
    texture.subresources = {{std::byte{128}, std::byte{255}, std::byte{128}, std::byte{255}}};
    image = upload_texture(presentation.device(), texture);
    material.textures["normalTexture"].semantic = TextureSemantic::Normal;
    material.textures["normalTexture"].uv_set = 19;
    textures.clear();
    textures["normalTexture"] = image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    MeshDraw mapped(presentation, context, gpu.lods[0].parts[0], material, textures,
                    TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    auto side = illumination;
    side.direction = {0, -1, 0};
    weights = {};
    stage = "tangent rest";
    const auto tangent_x = render(mapped, weights, std::span(&side, 1));
    weights[4] = 1;
    stage = "tangent changed";
    const auto tangent_y = render(mapped, weights, std::span(&side, 1));
    require(tangent_x[16 * 32 + 16][0] > 50 && tangent_y[16 * 32 + 16][0] < 15,
            "Morphed tangent or preserved handedness failed normal mapping");
    bool rejected = false;
    try {
        draw.draw(context, world, view, {}, nullptr, nullptr, nullptr, {}, nullptr,
                  std::array<float, 1>{1});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Incomplete morph vector was accepted");
}
