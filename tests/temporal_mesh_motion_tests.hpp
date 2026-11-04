#pragma once
#include "ambient_occlusion.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "motion_blur.hpp"
#include "surface_reflections.hpp"
// Uses the temporal probe's independent projection oracle and readback fixture.
namespace test_temporal_mesh_motion {
using Points = std::array<std::array<double, 3>, 3>;
inline constexpr Points base{{{{-.7, -.6, 0}}, {{-.5, .7, 0}}, {{.8, -.3, 0}}}};
inline constexpr Points delta{{{{0, 0, 0}}, {{.3, 0, .1}}, {{0, 0, 0}}}};
inline constexpr std::array<double, 3> first_weight{.75, .25, .5};
static forge::CameraView camera(bool perspective, bool old, unsigned width, unsigned height,
                                std::array<float, 2> jitter) {
    forge::Camera settings;
    settings.projection = perspective ? 0u : 1u;
    settings.orthographic_height = 4;
    settings.near_plane = .1;
    settings.far_plane = 100;
    forge::AffineTransform world;
    const double angle = old ? -.01 : .025;
    world.m[0] = world.m[10] = std::cos(angle);
    world.m[2] = std::sin(angle);
    world.m[8] = -std::sin(angle);
    world.m[3] = old ? -.03 : .04;
    world.m[11] = old ? 0 : -.1;
    auto result = forge::camera_view(settings, world, width, height);
    for (unsigned c = 0; c < 4; ++c) {
        result.projection[c] += jitter[0] * result.projection[12 + c];
        result.projection[4 + c] += jitter[1] * result.projection[12 + c];
    }
    return result;
}
static std::array<double, 3> transform(const forge::AffineTransform& matrix,
                                       const std::array<double, 3>& point) {
    std::array<double, 3> result{};
    for (unsigned r = 0; r < 3; ++r) {
        result[r] = matrix.m[4 * r + 3];
        for (unsigned c = 0; c < 3; ++c)
            result[r] += matrix.m[4 * r + c] * point[c];
    }
    return result;
}
static Points points(float weight, const forge::AffineTransform& world,
                     const forge::SkinPose* skin) {
    Points result{};
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
        auto point = base[vertex];
        for (unsigned axis = 0; axis < 3; ++axis)
            point[axis] += delta[vertex][axis] * weight;
        if (skin) {
            const auto a = transform(skin->palette[0], point);
            const auto b = transform(skin->palette[1], point);
            for (unsigned axis = 0; axis < 3; ++axis)
                result[vertex][axis] =
                    a[axis] * first_weight[vertex] + b[axis] * (1 - first_weight[vertex]);
        } else
            result[vertex] = transform(world, point);
    }
    return result;
}
static Clips project(const Points& points, const forge::CameraView& view) {
    Clips result{};
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
        std::array<double, 4> camera{};
        for (unsigned r = 0; r < 3; ++r) {
            const auto& basis = r == 0 ? view.right : r == 1 ? view.up : view.forward;
            for (unsigned c = 0; c < 3; ++c)
                camera[r] += basis[c] * (points[vertex][c] - view.position[c]);
        }
        camera[3] = 1;
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c)
                result[vertex][r] += float(view.projection[r * 4 + c] * camera[c]);
    }
    return result;
}
static Points wind_points(Points values, const forge::MeshWindSample& wind) {
    if (wind.enabled)
        for (auto& point : values) {
            const auto h = std::clamp((point[1] - wind.base_y) * wind.inverse_height, 0., 1.);
            point[0] += wind.displacement[0] * h * h;
            point[2] += wind.displacement[1] * h * h;
        }
    return values;
}
static void check_wind_normal(const Row& observed, const Clips& clip, const Points& points,
                              const forge::MeshWindSample& wind, unsigned x, unsigned y,
                              unsigned width, unsigned height, const forge::CameraView& view,
                              unsigned normal_map) {
    const double sx = 2 * (x + .5) / width - 1, sy = 1 - 2 * (y + .5) / height;
    const double ax = clip[0][0] / clip[0][3], ay = clip[0][1] / clip[0][3],
                 bx = clip[1][0] / clip[1][3], by = clip[1][1] / clip[1][3],
                 cx = clip[2][0] / clip[2][3], cy = clip[2][1] / clip[2][3];
    const double determinant = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    std::array<double, 3> weights{((by - cy) * (sx - cx) + (cx - bx) * (sy - cy)) / determinant,
                                  ((cy - ay) * (sx - cx) + (ax - cx) * (sy - cy)) / determinant, 0};
    weights[2] = 1 - weights[0] - weights[1];
    const auto dot = [](forge::Double3 a, forge::Double3 b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto unit = [](forge::Double3 v) {
        const auto length = std::hypot(v[0], v[1], v[2]);
        for (auto& item : v)
            item /= length;
        return v;
    };
    const auto cross = [](forge::Double3 a, forge::Double3 b) {
        return forge::Double3{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                              a[0] * b[1] - a[1] * b[0]};
    };
    forge::Double3 normal{}, tangent{}, bitangent{};
    for (unsigned i = 0; i < 3; ++i) {
        const double h = (points[i][1] - wind.base_y) * wind.inverse_height,
                     gradient = h > 0 && h < 1 ? 2 * h * wind.inverse_height : 0;
        const auto n = unit({0, gradient * wind.displacement[1], -1});
        forge::Double3 t{.6 + wind.displacement[0] * gradient * .8, .8,
                         wind.displacement[1] * gradient * .8};
        const auto projection = dot(n, t);
        for (unsigned axis = 0; axis < 3; ++axis)
            t[axis] -= n[axis] * projection;
        t = unit(t);
        const auto b = cross(n, t);
        for (unsigned axis = 0; axis < 3; ++axis) {
            normal[axis] += weights[i] * n[axis] / clip[i][3];
            tangent[axis] += weights[i] * t[axis] / clip[i][3];
            bitangent[axis] -= weights[i] * b[axis] / clip[i][3];
        }
    }
    normal = unit(normal);
    if (normal_map) {
        const auto projection = dot(normal, tangent);
        for (unsigned axis = 0; axis < 3; ++axis)
            tangent[axis] -= normal[axis] * projection;
        tangent = unit(tangent);
        auto b = cross(normal, tangent);
        const auto handed = dot(b, bitangent) < 0 ? -1. : 1.;
        for (auto& value : b)
            value *= handed;
        normal = normal_map == 1 ? tangent : b;
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto& basis = axis == 0 ? view.right : axis == 1 ? view.up : view.forward;
        const auto expected = basis[0] * normal[0] + basis[1] * normal[1] + basis[2] * normal[2];
        check(
            std::abs(expected - observed[axis]) < .002,
            "Wind surface frame differs from independent Jacobian/covector/interpolation oracles");
    }
}
static void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context, bool morph,
                     bool skinned, bool batch, bool perspective, bool reflection = false,
                     bool normal = false, bool rough = false, bool wind = false,
                     unsigned normal_map = 0) {
    using namespace forge;
    constexpr unsigned width = 192, height = 128;
    auto* device = presentation.device();
    MeshPart part;
    part.vertices = 3;
    part.indices = {0, 1, 2};
    std::vector<float> positions, deltas;
    for (unsigned vertex = 0; vertex < 3; ++vertex)
        for (unsigned axis = 0; axis < 3; ++axis) {
            positions.push_back(float(base[vertex][axis]));
            deltas.push_back(float(delta[vertex][axis]));
        }
    part.streams = {{"POSITION", 3, positions},
                    {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1}}};
    if (wind) {
        part.streams.push_back(
            {"TANGENT", 4, std::vector<float>{.6f, .8f, 0, -1, .6f, .8f, 0, -1, .6f, .8f, 0, -1}});
        part.streams.push_back({"TEXCOORD_0", 2, std::vector<float>(6)});
    }
    if (skinned) {
        part.joint_palette = {0, 1};
        std::vector<std::uint32_t> joints;
        std::vector<float> weights;
        for (unsigned vertex = 0; vertex < 3; ++vertex) {
            joints.insert(joints.end(), {0, 1, 0, 0});
            weights.insert(weights.end(),
                           {float(first_weight[vertex]), float(1 - first_weight[vertex]), 0, 0});
        }
        part.streams.push_back({"JOINTS_0", 4, joints});
        part.streams.push_back({"WEIGHTS_0", 4, weights});
    }
    if (morph)
        part.morph_targets = {{{"POSITION", 3, deltas}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    if (morph) {
        mesh.morph_names = {"Bend"};
        mesh.morph_defaults = {0};
    }
    const auto gpu = upload_mesh(device, mesh);
    auto material = asset_detail::engine_material_resource(
                        engine_material(reflection || normal ? EngineMaterial::Default
                                                             : EngineMaterial::LegacyBlockout))
                        .values;
    material.double_sided = true;
    if (reflection || normal) {
        material.alpha = MaterialAlpha::Mask;
        material.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.1f}};
        material.parameters["emissiveFactor"] = {MaterialParameterType::LinearColor3,
                                                 {.8f, .6f, .4f}};
    }
    MeshDraw::Textures textures;
    RefCntAutoPtr<ITexture> wind_normal_image;
    if (normal_map) {
        material.textures["normalTexture"].semantic = TextureSemantic::Normal;
        forge::TextureData image;
        image.width = image.height = 1;
        image.format = TextureFormat::RGBA16Float;
        image.subresources = {std::vector<std::byte>(8)};
        const std::array<float, 4> value = normal_map == 1 ? std::array<float, 4>{1, .5f, .5f, 1}
                                                           : std::array<float, 4>{.5f, 1, .5f, 1};
        for (unsigned lane = 0; lane < 4; ++lane) {
            const auto bits = test_bloom::half(value[lane]);
            std::memcpy(image.subresources[0].data() + lane * 2, &bits, 2);
        }
        wind_normal_image = upload_texture(device, image);
        textures["normalTexture"] = wind_normal_image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }
    auto fetch =
        mesh_vertex_fetch(gpu.lods[0].parts[0], prepare_pbr_material(material), true, true);
    fetch.wind = wind;
    const auto program = mesh_draw_shader(fetch, prepare_pbr_material(material), false,
                                          emulated_resource_arrays(device->GetDeviceInfo())
                                              ? MaterialSamplerBinding::NamedElements
                                              : MaterialSamplerBinding::Array,
                                          nullptr, {}, false, {}, true, reflection, normal);
    const auto shader_root = std::filesystem::current_path() / "temporal-filter-shaders";
    std::filesystem::create_directories(shader_root);
    const auto prefix = std::to_string(morph) + "-" + std::to_string(skinned) +
                        (reflection ? "-reflection"
                         : normal   ? "-normal"
                                    : "-indirect") +
                        (wind ? "-wind" : "") + "-map" + std::to_string(normal_map);
    for (const auto& [stage, source] :
         {std::pair{"vertex", program.vertex}, std::pair{"pixel", program.pixel},
          std::pair{"geometry", program.geometry}}) {
        std::ofstream file(shader_root / (prefix + "-" + stage + ".hlsl"));
        file << source;
        check(bool(file), "Temporal shader source receipt failed");
    }
    MeshDraw draw(presentation, context, gpu.lods[0].parts[0], material, textures,
                  TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, true, SHADER_COMPILER_DEFAULT,
                  SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                  reflection ? MeshDrawProfile::MeshTemporalReflection
                  : normal   ? MeshDrawProfile::MeshTemporalNormal
                             : MeshDrawProfile::MeshTemporalIndirect,
                  false, wind);
    check(draw.supports_instances() == (!morph && !skinned),
          "Temporal draw changed the static/deformed instancing policy");
    auto color = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                         BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    auto motion_target = texture(device, width, height, TEX_FORMAT_RGBA32_FLOAT,
                                 BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    auto indirect = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                            BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    auto occluded = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                            BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    std::array<RefCntAutoPtr<ITexture>, 3> reflection_targets;
    for (unsigned i = 0; i < (reflection ? 3u : normal ? 1u : 0u); ++i)
        reflection_targets[i] = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                                        BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    AmbientOcclusion occlusion(presentation);
    SurfaceReflections reflections(presentation);
    std::vector<std::array<RefCntAutoPtr<ITexture>, 2>> source_targets;
    std::vector<SurfaceReflections::SourceLevel> source_views;
    RefCntAutoPtr<ITexture> rough_indirect;
    if (rough) {
        const auto plan = reflection_source_plan(width, height);
        for (unsigned i = 0; i < plan.count; ++i) {
            source_targets.push_back(
                {texture(device, plan.levels[i].width, plan.levels[i].height,
                         TEX_FORMAT_RGBA16_FLOAT, BIND_RENDER_TARGET | BIND_SHADER_RESOURCE),
                 texture(device, plan.levels[i].width, plan.levels[i].height, TEX_FORMAT_RG32_FLOAT,
                         BIND_RENDER_TARGET | BIND_SHADER_RESOURCE)});
            source_views.push_back({source_targets.back()[0], source_targets.back()[1]});
        }
        rough_indirect = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                                 BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    }
    auto depth = texture(device, width, height, TEX_FORMAT_D32_FLOAT,
                         BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE);
    const auto old_jitter = TemporalFilter::jitter(4, width, height);
    const auto jitter = TemporalFilter::jitter(5, width, height);
    const auto previous_view = camera(perspective, true, width, height, old_jitter);
    const auto current_view = camera(perspective, false, width, height, jitter);
    AffineTransform previous_world, current_world, added_world;
    previous_world.m[3] = batch ? -1.7 : -.1;
    previous_world.m[11] = 3.1;
    current_world.m[3] = batch ? -1.4 : .2;
    current_world.m[11] = 3;
    added_world.m[3] = 1.6;
    added_world.m[11] = 3;
    std::array<AffineTransform, 2> old_joints, current_joints, inverse{};
    old_joints[0].m[3] = -.08;
    old_joints[0].m[11] = 3;
    current_joints[0].m[3] = .1;
    current_joints[0].m[11] = 3;
    for (unsigned old = 0; old < 2; ++old) {
        auto& joint = old ? old_joints[1] : current_joints[1];
        const double angle = old ? -.1 : .2;
        joint.m[0] = joint.m[10] = std::cos(angle);
        joint.m[2] = std::sin(angle);
        joint.m[8] = -std::sin(angle);
        joint.m[3] = old ? -.2 : -.15;
        joint.m[11] = 3.2;
    }
    auto source_bounds = part.bounds;
    source_bounds.minimum[0] -= .5f;
    source_bounds.maximum[0] += .5f;
    source_bounds.maximum[2] += .5f;
    std::optional<SkinPose> old_skin, current_skin;
    if (skinned) {
        old_skin = prepare_skin_pose(old_joints, inverse, part.joint_palette, source_bounds);
        current_skin =
            prepare_skin_pose(current_joints, inverse, part.joint_palette, source_bounds);
    }
    const float old_weight = morph ? .15f : 0, current_weight = morph ? .75f : 0;
    MeshWind wind_config;
    wind_config.enabled = wind;
    wind_config.amplitude = .6;
    wind_config.height = 1.4;
    wind_config.direction_x = 3;
    wind_config.direction_z = 4;
    wind_config.phase = 65;
    const auto old_wind = sample_mesh_wind(wind_config, previous_world, .2),
               current_wind = sample_mesh_wind(wind_config, current_world, .7),
               added_wind = sample_mesh_wind(wind_config, added_world, .7);
    const auto old_points =
        wind_points(points(old_weight, previous_world, old_skin ? &*old_skin : nullptr), old_wind);
    const auto current_points =
        wind_points(points(current_weight, current_world, current_skin ? &*current_skin : nullptr),
                    current_wind);
    const auto old_clip = project(old_points, previous_view);
    const auto current_clip = project(current_points, current_view);
    DisplayResolve display(presentation);
    MotionBlur blur(presentation);
    auto blurred = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                           BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    for (unsigned frame = 0; frame < 2; ++frame) {
        presentation.begin_frame();
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        std::vector<ITextureView*> targets{
            color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            motion_target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            indirect->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
        for (const auto& target : reflection_targets)
            if (target)
                targets.push_back(target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
        context->SetRenderTargets(Uint32(targets.size()), targets.data(), dsv,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float dark[]{.02f, .02f, .02f, 1}, empty[]{0, 0, 1, 0};
        context->ClearRenderTarget(targets[0], dark, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(targets[1], empty, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float zero[4]{};
        for (std::size_t i = 2; i < targets.size(); ++i)
            context->ClearRenderTarget(targets[i], zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, float(width), float(height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        const auto& view = frame ? current_view : previous_view;
        MeshDraw::Motion motion;
        motion.valid = frame != 0;
        motion.previous_view = frame ? &previous_view : nullptr;
        motion.previous_world = frame && !batch ? &previous_world : nullptr;
        motion.previous_skin = frame && old_skin ? &*old_skin : nullptr;
        motion.previous_wind = frame ? &old_wind : nullptr;
        motion.previous_morph_weights =
            frame && morph ? std::span(&old_weight, 1) : std::span<const float>{};
        motion.jitter = frame ? jitter : old_jitter;
        motion.previous_jitter = old_jitter;
        std::vector<MeshDraw::Instance> instances;
        if (batch) {
            if (frame)
                instances.push_back({added_world, {}, nullptr, nullptr, &added_wind, nullptr});
            instances.push_back({frame ? current_world : previous_world,
                                 {},
                                 nullptr,
                                 frame ? &previous_world : nullptr,
                                 frame ? &current_wind : &old_wind,
                                 frame ? &old_wind : nullptr});
        }
        const auto& world =
            instances.empty() ? frame ? current_world : previous_world : instances.front().world;
        const auto* skin = frame      ? current_skin ? &*current_skin : nullptr
                           : old_skin ? &*old_skin
                                      : nullptr;
        const auto weights = morph
                                 ? frame ? std::span(&current_weight, 1) : std::span(&old_weight, 1)
                                 : std::span<const float>{};
        draw.draw(context, world, view, {}, nullptr, nullptr, nullptr, {}, nullptr, weights, skin,
                  instances, {}, {}, nullptr, nullptr, &motion, {}, nullptr, {}, {},
                  frame ? &current_wind : &old_wind);
        const auto point = check_motion_at(device, context, motion_target, width, height,
                                           frame ? current_clip : old_clip, old_clip,
                                           frame ? jitter : old_jitter, old_jitter, frame != 0);
        if (reflection || normal) {
            const auto at = point[1] * width + point[0];
            const auto normals = forge::test_bloom::read(device, context, reflection_targets[0]);

            const auto& n = normals[at];
            check(std::abs(std::hypot(n[0], n[1], n[2]) - 1) < .002 && std::abs(n[3] - .1f) < .001,
                  "Posed/instanced reflection material lost its current normal or roughness");
            if (wind)
                check_wind_normal(n, frame ? current_clip : old_clip,
                                  frame ? current_points : old_points,
                                  frame ? current_wind : old_wind, point[0], point[1], width,
                                  height, view, normal_map);
            if (reflection) {
                const auto response =
                    forge::test_bloom::read(device, context, reflection_targets[1]);
                const auto specular =
                    forge::test_bloom::read(device, context, reflection_targets[2]);
                check(response[at][0] + response[at][1] + response[at][2] > .00001 &&
                          specular[at][0] == 0 && specular[at][1] == 0 && specular[at][2] == 0,
                      "Posed/instanced reflection material fabricated an environment or lost "
                      "response");
            }
        }
        std::optional<std::array<unsigned, 2>> added;
        if (batch && frame) {
            const auto clip =
                project(wind_points(points(0, added_world, nullptr), added_wind), current_view);
            added = check_motion_at(device, context, motion_target, width, height, clip, clip,
                                    jitter, old_jitter, false);
        }
        auto* output =
            display.resolve(context, color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0);
        const auto pixels =
            forge::test_sprite_draw::read_pixels(device, context, output->GetTexture());
        check(pixels[point[1] * width + point[0]][0] > 100,
              "Engine temporal Mesh did not draw its foreground");
        if (added)
            check(pixels[(*added)[1] * width + (*added)[0]][0] > 100,
                  "New batch member was omitted instead of marking motion invalid");
        AmbientOcclusion::Input spatial;
        spatial.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        spatial.indirect = indirect->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        spatial.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        spatial.projection = view.projection;
        spatial.settings.enabled = true;
        if (normal) {
            spatial.settings.normal_influence = 1;
            spatial.surface = reflection_targets[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }
        occlusion.apply(context, spatial, occluded->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
        auto* spatial_output =
            display.resolve(context, occluded->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0);
        check(forge::test_sprite_draw::read_pixels(device, context, spatial_output->GetTexture()) ==
                  pixels,
              "Occlusion changed unlit posed/instanced Mesh pixels or alpha");
        if (rough) {
            SurfaceReflections::Input reflection_input;
            reflection_input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            reflection_input.indirect = indirect->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            reflection_input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            reflection_input.surface =
                reflection_targets[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            reflection_input.response =
                reflection_targets[1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            reflection_input.specular =
                reflection_targets[2]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            reflection_input.projection = view.projection;
            reflection_input.settings = {true, 8, .1f, 1, .9f, true};
            reflection_input.sources = source_views;
            reflections.build_sources(context, reflection_input, source_views);
            reflections.apply(context, reflection_input,
                              occluded->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                              rough_indirect->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
            auto* result =
                display.resolve(context, occluded->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0);
            check(forge::test_sprite_draw::read_pixels(device, context, result->GetTexture()) ==
                      pixels,
                  "Rough reflections invented geometry for a posed/instanced plane");
            const auto source = forge::test_bloom::read(device, context, source_targets[0][0]);
            check(std::any_of(source.begin(), source.end(), [](const auto& p) { return p[3] > 0; }),
                  "Rough source reduction lost posed/instanced current depth");
        }
        // Consume the actual generated Mesh motion, including posed/instanced
        // producers. The independent motion oracle above validates those inputs.
        MotionBlur::Input input;
        input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.motion = motion_target->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.projection = {view.projection[10], view.projection[11], view.projection[14],
                            view.projection[15]};
        input.settings = {true, 1, 32};
        blur.apply(context, input, blurred->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
        auto* exposed =
            display.resolve(context, blurred->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0);
        const auto filtered =
            forge::test_sprite_draw::read_pixels(device, context, exposed->GetTexture());
        if (!frame)
            check(filtered == pixels, "First real Mesh frame blurred without correspondence");
        if (added)
            check(filtered[(*added)[1] * width + (*added)[0]] ==
                      pixels[(*added)[1] * width + (*added)[0]],
                  "New instance consumed an unrelated member's completed motion");
        for (std::size_t i = 0; i < pixels.size(); ++i)
            check(filtered[i][3] == pixels[i][3],
                  "Deformed/instanced motion exposure changed current alpha");
        presentation.end_frame(context);
        context->FinishFrame();
        device->ReleaseStaleResources();
    }
    std::cout << "Shared Mesh motion: morph=" << morph << " skin=" << skinned << " batch=" << batch
              << " perspective=" << perspective << " reflection=" << reflection
              << " normal=" << normal << " rough=" << rough << " wind=" << wind << " passed\n";
}
static void exercise_wind(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    for (bool perspective : {false, true})
        for (bool batch : {false, true})
            exercise(presentation, context, false, false, batch, perspective, false, true, false,
                     true);
    for (unsigned normal_map : {1u, 2u})
        exercise(presentation, context, false, false, false, true, false, true, false, true,
                 normal_map);
}
static void exercise_all(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    for (bool perspective : {false, true}) {
        for (bool skin : {false, true})
            for (bool morph : {false, true})
                exercise(presentation, context, morph, skin, false, perspective);
        exercise(presentation, context, false, false, true, perspective);
        for (bool skin : {false, true})
            for (bool morph : {false, true})
                exercise(presentation, context, morph, skin, false, perspective, true);
        exercise(presentation, context, false, false, true, perspective, true);
    }
}
static void exercise_rough(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    for (bool perspective : {false, true}) {
        for (bool skin : {false, true})
            for (bool morph : {false, true})
                exercise(presentation, context, morph, skin, false, perspective, true, false, true);
        exercise(presentation, context, false, false, true, perspective, true, false, true);
    }
}
static void exercise_normals(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    for (bool perspective : {false, true}) {
        for (bool skin : {false, true})
            for (bool morph : {false, true})
                exercise(presentation, context, morph, skin, false, perspective, false, true);
        exercise(presentation, context, false, false, true, perspective, false, true);
    }
}
} // namespace test_temporal_mesh_motion
