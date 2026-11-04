#pragma once
#include "shader_diligent.hpp"
#include "sprite_frame_mip_render_tests.hpp"
#include "sprite_image_render_tests.hpp"
#include <forge/material_graph.hpp>

namespace forge::test_sprite_surface {
// Windows acceptance uses real worker-compatible DXBC, reflection, reload and
// the production MeshDraw adapter. Source-only probes do not invoke this fixture.
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& images) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    for (bool graph : {false, true}) {
        ShaderProgramSource program;
        ShaderSources sources;
        if (graph) {
            const auto compiled = compile_material_graph(create_sprite_material_graph());
            program.surface = compiled.surface;
            program.stages = {{ShaderStage::Pixel, "surface/main.hlsl", "ForgeGraphSurface"}};
            sources["surface/main.hlsl"] = compiled.source;
        } else {
            program.surface.emplace();
            program.surface->sprite_image = "image";
            program.surface->uv_sets = {0};
            program.surface->textures["image"].semantic = TextureSemantic::Color;
            program.surface->textures["detail"].semantic = TextureSemantic::Color;
            program.surface->parameters["gain"] = {MaterialParameterType::Scalar, {1}};
            program.stages = {{ShaderStage::Pixel, "surface/main.hlsl", "Shade"}};
            sources["surface/main.hlsl"] =
                "float4 Shade(ForgeSpriteInput input){"
                "return ForgeSample_image(input,ForgeUV0(input))*ForgeSample_detail(input)*"
                "ForgeParameter_gain()*input.Color;}";
        }
        const auto compiled = asset_detail::compile_diligent_shader(device, program, sources, {});
        auto bytes = encode_shader(compiled.data);
        auto decoded = decode_shader(bytes);
        const auto realized = asset_detail::realize_diligent_shader(device, decoded);
        check(realized.stages.size() == 2 && decoded.surface->sprite_image &&
                  decoded.surface_wrapper_version == current_surface_wrapper_version,
              "Native Sprite cook/reload lost its image/domain or two output roles");
        for (const auto& stage : decoded.stages) {
            check(sprite_surface_role(stage.role), "Sprite program acquired a Mesh stage");
            const auto layout = surface_binding_layout(*decoded.surface, stage.reflection,
                                                       stage.role, decoded.surface_wrapper_version);
            const auto expected = graph ? std::vector<std::string>{*decoded.surface->sprite_image}
                                        : std::vector<std::string>{"detail", "image"};
            check(layout.textures == expected,
                  "Native Sprite image reflection lost its stable key");
        }
        MaterialShaderSnapshot snapshot{
            {AssetId::generate()}, std::string(64, 'a'), std::move(decoded)};
        const auto folder = images / (graph ? "graph" : "hlsl");
        test_sprite_draw::exercise(presentation, context, readback, folder / "raw", &snapshot);
        test_sprite_image::exercise(presentation, context, readback, folder / "fill", &snapshot);
        test_sprite_frame_mip_render::exercise(presentation, context, readback, folder / "mips",
                                               &snapshot);
        auto invalid = sources;
        invalid["surface/main.hlsl"] = "This is not HLSL @";
        bool rejected = false;
        try {
            (void)asset_detail::compile_diligent_shader(device, program, invalid, {});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && encode_shader(snapshot.program) == bytes,
              "Invalid Sprite source changed the previously selected immutable program");
    }
}
} // namespace forge::test_sprite_surface
