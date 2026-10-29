#pragma once
#include "ambient_occlusion.hpp"
#include "motion_blur.hpp"
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
static void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context, bool morph,
                     bool skinned, bool batch, bool perspective) {
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
    auto material =
        asset_detail::engine_material_resource(engine_material(EngineMaterial::LegacyBlockout))
            .values;
    material.double_sided = true;
    const auto fetch =
        mesh_vertex_fetch(gpu.lods[0].parts[0], prepare_pbr_material(material), true, true);
    const auto program = mesh_draw_shader(fetch, prepare_pbr_material(material), false,
                                          emulated_resource_arrays(device->GetDeviceInfo())
                                              ? MaterialSamplerBinding::NamedElements
                                              : MaterialSamplerBinding::Array);
    const auto shader_root = std::filesystem::current_path() / "temporal-filter-shaders";
    std::filesystem::create_directories(shader_root);
    const auto prefix = std::to_string(morph) + "-" + std::to_string(skinned);
    for (const auto& [stage, source] :
         {std::pair{"vertex", program.vertex}, std::pair{"pixel", program.pixel},
          std::pair{"geometry", program.geometry}}) {
        std::ofstream file(shader_root / (prefix + "-" + stage + ".hlsl"));
        file << source;
        check(bool(file), "Temporal shader source receipt failed");
    }
    MeshDraw draw(presentation, context, gpu.lods[0].parts[0], material, {},
                  TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, true, SHADER_COMPILER_DEFAULT,
                  SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                  MeshDrawProfile::MeshTemporalIndirect);
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
    AmbientOcclusion occlusion(presentation);
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
    const auto old_points = points(old_weight, previous_world, old_skin ? &*old_skin : nullptr);
    const auto current_points =
        points(current_weight, current_world, current_skin ? &*current_skin : nullptr);
    const auto old_clip = project(old_points, previous_view);
    const auto current_clip = project(current_points, current_view);
    DisplayResolve display(presentation);
    MotionBlur blur(presentation);
    auto blurred = texture(device, width, height, TEX_FORMAT_RGBA16_FLOAT,
                           BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    for (unsigned frame = 0; frame < 2; ++frame) {
        presentation.begin_frame();
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        std::array<ITextureView*, 3> targets{
            color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            motion_target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            indirect->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
        context->SetRenderTargets(3, targets.data(), dsv,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float dark[]{.02f, .02f, .02f, 1}, empty[]{0, 0, 1, 0};
        context->ClearRenderTarget(targets[0], dark, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(targets[1], empty, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float zero[4]{};
        context->ClearRenderTarget(targets[2], zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
        motion.previous_morph_weights =
            frame && morph ? std::span(&old_weight, 1) : std::span<const float>{};
        motion.jitter = frame ? jitter : old_jitter;
        motion.previous_jitter = old_jitter;
        std::vector<MeshDraw::Instance> instances;
        if (batch) {
            if (frame)
                instances.push_back({added_world, {}, nullptr, nullptr});
            instances.push_back({frame ? current_world : previous_world,
                                 {},
                                 nullptr,
                                 frame ? &previous_world : nullptr});
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
                  instances, {}, {}, nullptr, nullptr, &motion);
        const auto point = check_motion_at(device, context, motion_target, width, height,
                                           frame ? current_clip : old_clip, old_clip,
                                           frame ? jitter : old_jitter, old_jitter, frame != 0);
        std::optional<std::array<unsigned, 2>> added;
        if (batch && frame) {
            const auto clip = project(points(0, added_world, nullptr), current_view);
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
        occlusion.apply(context, spatial, occluded->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
        auto* spatial_output =
            display.resolve(context, occluded->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0);
        check(forge::test_sprite_draw::read_pixels(device, context, spatial_output->GetTexture()) ==
                  pixels,
              "Occlusion changed unlit posed/instanced Mesh pixels or alpha");
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
              << " perspective=" << perspective << " passed\n";
}
static void exercise_all(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    for (bool perspective : {false, true}) {
        for (bool skin : {false, true})
            for (bool morph : {false, true})
                exercise(presentation, context, morph, skin, false, perspective);
        exercise(presentation, context, false, false, true, perspective);
    }
}
} // namespace test_temporal_mesh_motion
