#pragma once
#include "gallery_asset_publication.hpp"
#include "material_authoring.hpp"
#include "material_document.hpp"
#include "mesh_render_host.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_gpu_material_checks.hpp"
#include <chrono>
#include <fstream>
#include <thread>
namespace forge::test_particle_palette_scene {
using namespace Diligent;
using test_bloom::check;
using Pixels = std::vector<test_bloom::Pixel>;
struct Set {
    std::array<AssetRef<ParticleEffectAsset>, 5> gpu, cpu;
    AssetRef<ParticleEffectAsset> old_profile, geometry;
};
inline void exercise(DiligentPresentation& presentation, IDeviceContext* context,
                     const std::filesystem::path& scratch) {
    auto* device = presentation.device();
    check(device && context, "Published palette device unavailable");
    const auto root = std::filesystem::absolute(scratch);
    const auto marker = root / ".forge-palette-scene-test";
    check(!std::filesystem::is_symlink(root), "Palette scratch cannot be a symlink");
    if (std::filesystem::exists(root)) {
        check(!std::filesystem::is_symlink(marker) && std::filesystem::is_regular_file(marker),
              "Refusing to replace unowned palette scratch");
        std::ifstream input(marker);
        std::string identity;
        std::getline(input, identity);
        check(identity == "FORGE particle palette Scene fixture",
              "Palette scratch ownership mismatch");
        std::filesystem::remove_all(root);
    }
    std::filesystem::create_directories(root);
    {
        std::ofstream output(marker);
        output << "FORGE particle palette Scene fixture\n";
        check(bool(output), "Cannot mark palette scratch ownership");
    }
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
#ifdef _WIN32
    const ImportTarget target{"windows", "d3d12", "desktop"};
#else
    const ImportTarget target{"linux", "none", "cpu"};
#endif
    AssetImportService material_imports(lease, material_import_registry(), target);
    AssetImportService effect_imports(lease, particle_effect_import_registry(), target);
    std::array<Set, 3> sets;
    const std::array models{"forge.gltf.unlit.v1", "forge.gltf.metallic-roughness.v1",
                            "forge.gltf.specular-glossiness.v1"};
    for (unsigned model = 0; model < models.size(); ++model)
        for (unsigned i = 0; i < 5; ++i) {
            const auto prefix = "M" + std::to_string(model) + "-" + std::to_string(i);
            const auto material_path =
                std::filesystem::path("Assets") / (prefix + ".material.json");
            auto document = MaterialDocument::create(lease, material_path);
            const auto material_id = document->source().asset();
            MaterialData value;
            value.model = models[model];
            value.alpha = MaterialAlpha::Blend;
            value.depth_write = false;
            value.double_sided = true;
            value.parameters["baseColorFactor"] = {
                MaterialParameterType::LinearColor4,
                {.125f + .125f * i, .75f - .125f * i, .25f + .0625f * i, .5f}};
            if (model == 1) {
                value.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {.25f}};
                value.parameters["roughnessFactor"] = {MaterialParameterType::Scalar,
                                                       {.25f + .125f * i}};
            }
            if (model == 2)
                value.parameters["glossinessFactor"] = {MaterialParameterType::Scalar,
                                                        {.25f + .125f * i}};
            const auto role = model == 2 ? "diffuseTexture" : "baseColorTexture";
            auto& slot = value.textures[role];
            slot.semantic = TextureSemantic::Color;
            slot.offset = {.125f * i, -.0625f * i};
            slot.scale = {1.f + .25f * i, i % 2 ? -.75f : 1.f};
            slot.rotation = .125f * i;
            slot.sampler.min = slot.sampler.mag =
                i % 2 ? TextureFilter::Nearest : TextureFilter::Linear;
            slot.sampler.u = i > 1 ? TextureWrap::ClampEdge : TextureWrap::Repeat;
            const auto texture =
                engine_texture(i % 2 ? EngineTexture::White : EngineTexture::Checker);
            const auto encoded = material_values_document(value);
            document->edit_source(
                document->revision(), "Published palette fixture", [&](auto& source) {
                    for (const auto* key :
                         {"model", "alpha", "depth_write", "double_sided", "parameters"})
                        source.document["overrides"][key] = encoded.at(key);
                    source.document["overrides"]["textures"][role] = {
                        {"asset", texture.id}, {"slot", encoded.at("textures").at(role)}};
                });
            document->save();
            test::publish_gallery_asset(material_imports, material_path,
                                        prepare_material_publication, std::chrono::seconds(30),
                                        material_id);
            const auto effect = [&](const char* suffix, bool cpu, bool old, bool geometry) {
                const auto path =
                    std::filesystem::path("Assets") / (prefix + suffix + ".particles.json");
                auto authored = ParticleEffectDocument::create(lease, path);
                const auto id = authored->source().asset();
                ParticleEffectData data;
                data.version = 46;
                data.mesh = engine_primitive(geometry ? 0 : 8);
                data.material = {material_id};
                data.emission = ParticleEmission::Burst;
                data.capacity = data.burst_count = 1;
                data.lifetime = 2;
                data.direction = {0, 0, 1};
                data.speed_min = data.speed_max = 0;
                data.acceleration = {};
                data.cone_angle = 0;
                data.size_start = data.size_end = .75;
                data.color_start = data.color_end = {1, 1, 1};
                data.opacity_start = data.opacity_end = .75;
                const auto fields = encode_particle_effect(data);
                authored->edit_source(
                    authored->revision(), "Published palette fixture", [&](auto& source) {
                        for (auto& [key, field] : source.document.items())
                            if (key != "asset_id" && key != "format" && key != "version" &&
                                key != "profile" && fields.contains(key))
                                field = fields.at(key);
                        source.enable_gpu_material_palette(true);
                        source.enable_gpu_opacity(true);
                        source.document["opacity_start"] = .75;
                        source.document["opacity_end"] = .75;
                        if (cpu)
                            source.enable_opacity(true);
                        if (old)
                            source.enable_gpu_material_palette(false);
                    });
                authored->save();
                test::publish_gallery_asset(effect_imports, path,
                                            prepare_particle_effect_publication,
                                            std::chrono::seconds(30), id);
                return AssetRef<ParticleEffectAsset>{id};
            };
            sets[model].gpu[i] = effect("-GPU", false, false, false);
            sets[model].cpu[i] = effect("-CPU", true, false, false);
            if (model == 0 && i == 1) {
                sets[model].old_profile = effect("-Old", false, true, false);
                sets[model].geometry = effect("-Geometry", false, false, true);
            }
        }
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    MeshSceneRenderer gpu(host, TEX_FORMAT_RGBA32_FLOAT), cpu(host, TEX_FORMAT_RGBA32_FLOAT);
    constexpr unsigned extent = 64;
    TextureDesc desc;
    desc.Name = "FORGE published palette comparison";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = extent;
    desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Published palette targets unavailable");
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    for (unsigned i = 0; i < 5; ++i) {
        RenderParticleEmitter emitter;
        emitter.entity = EntityId::generate();
        emitter.time = .5;
        emitter.world.m[3] = .03125 * i;
        emitter.world.m[11] = 3 + .25 * i;
        scene.particles.push_back(emitter);
    }
    scene.particles.resize(4);
    Camera settings;
    settings.projection = 1;
    settings.orthographic_height = 2;
    unsigned frames = 0;
    const std::array<float, 4> background{.02f, .04f, .08f, 1};
    const auto render = [&](MeshSceneRenderer& selected, const CameraView& view,
                            bool refused = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            selected.update(scene);
            check(std::chrono::steady_clock::now() < deadline,
                  "Published palette resources stalled");
            if (selected.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (selected.pending());
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, background.data(),
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport area{0, 0, float(extent), float(extent), 0, 1};
        context->SetViewports(1, &area, extent, extent);
        selected.draw(scene, view, UINT32_MAX, color, dsv);
        if (!refused && !selected.diagnostics().empty())
            throw std::runtime_error(nlohmann::json{{"frame", frames},
                                                    {"bundles", selected.bundle_count()},
                                                    {"reason", selected.diagnostics().front().text}}
                                         .dump());
        if (refused) {
            check(std::any_of(selected.diagnostics().begin(), selected.diagnostics().end(),
                              [](const auto& d) {
                                  return d.text.find("GPU sorted alpha unavailable") !=
                                         std::string::npos;
                              }),
                  "Published palette lost its refusal diagnostic");
            check(selected.draw_stats().calls == 0, "Refused published palette partially rendered");
        }
        auto pixels = test_bloom::read(device, context, color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        ++frames;
        if (refused)
            for (const auto& pixel : pixels)
                check(pixel == background, "Refused published palette left geometry");
        return pixels;
    };
    const auto equal = [&](const Pixels& actual, const Pixels& expected) {
        unsigned visible = 0;
        for (unsigned p = 0; p < actual.size(); ++p)
            for (unsigned c = 0; c < 4; ++c) {
                if (!std::isfinite(actual[p][c]) || std::abs(actual[p][c] - expected[p][c]) > 3e-5f)
                    throw std::runtime_error(nlohmann::json{
                        {"frame", frames},
                        {"pixel", p},
                        {"actual", actual[p]},
                        {"expected", expected[p]},
                        {"gpu_draws", gpu.draw_stats().calls},
                        {"cpu_draws",
                         cpu.draw_stats().calls}}.dump());
                visible += std::abs(expected[p][c] - background[c]) > .001f;
            }
        check(visible > 100, "Published palette comparison had no visible geometry");
    };
    const auto select = [&](const auto& refs) {
        for (unsigned i = 0; i < scene.particles.size(); ++i)
            scene.particles[i].emitter.effect = refs[i];
    };
    for (unsigned model = 0; model < models.size(); ++model)
        for (bool reverse : {false, true}) {
            AffineTransform eye;
            if (reverse) {
                eye.m[0] = eye.m[10] = -1;
                eye.m[11] = 10;
            }
            const auto view = camera_view(settings, eye, extent, extent);
            scene.lights.clear();
            Light lamp;
            lamp.intensity = 2;
            scene.lights.push_back({EntityId::generate(), light_view(lamp, eye), {}, {}});
            select(sets[model].cpu);
            const auto expected = render(cpu, view);
            select(sets[model].gpu);
            equal(render(gpu, view), expected);
            check(gpu.draw_stats().calls == 1 && gpu.draw_stats().instances == 4,
                  "Published palette did not draw one complete group");
            equal(render(gpu, view), expected);
            if (model == 0 && !reverse) {
                {
                    MeshSceneRenderer exact(host, TEX_FORMAT_RGBA32_FLOAT, mesh_pose_payload_limit,
                                            5);
                    equal(render(exact, view), expected);
                    equal(render(exact, view), expected);
                }
                auto fifth = scene.particles.front();
                fifth.entity = EntityId::generate();
                fifth.emitter.effect = sets[model].gpu[4];
                fifth.world.m[11] = 4;
                scene.particles.push_back(fifth);
                render(gpu, view, true);
                scene.particles.pop_back();
                equal(render(gpu, view), expected);
                scene.particles[1].emitter.effect = sets[model].old_profile;
                render(gpu, view, true);
                select(sets[model].gpu);
                equal(render(gpu, view), expected);
                scene.particles[1].emitter.effect = sets[model].geometry;
                render(gpu, view, true);
                select(sets[model].gpu);
                equal(render(gpu, view), expected);
            }
        }
    context->WaitForIdle();
    std::cout << "MATERIAL_PALETTE_PUBLISHED frames=" << frames
              << "; normal publication/catalog/resources, three models/four Materials, distinct "
                 "textures/UV/samplers, independent CPU alpha, reverse cameras, exact draw budget "
                 "reuse and complete refusal/recovery\n";
}
} // namespace forge::test_particle_palette_scene
