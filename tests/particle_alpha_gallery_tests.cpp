#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "gallery_asset_publication.hpp"
#include "material_authoring.hpp"
#include "material_document.hpp"
#include "material_selection.hpp"
#include "paint_texture_source.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_effect_resource.hpp"
#include "particle_gpu_plan.hpp"
#include "particle_path.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include "texture_authoring.hpp"
#include "transform_animation_authoring.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/physics.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <set>
using namespace forge;
namespace {
struct Example {
    const char* scene;
    const char* effect;
    bool opacity;
    bool billboards = false;
    bool gpu = false;
    bool graph = false;
    bool trails = false;
    bool collision = false;
    bool weighted = false;
    bool gpu_opacity = false;
    bool sorted = false;
    bool scene_collision = false;
    bool scene_collision_2d = false;
    bool world_trails = false;
    bool impacts = false;
    bool resting = false;
};
constexpr Example alpha{"Scenes/240-sorted-alpha-particles.scene.json",
                        "Assets/Particles/Alpha/GlassCloud.particles.json", false};
constexpr Example fading{"Scenes/241-particle-opacity.scene.json",
                         "Assets/Particles/Alpha/FadingCloud.particles.json", true};
constexpr Example quads{"Scenes/242-camera-aligned-particles.scene.json",
                        "Assets/Particles/Billboards/SoftCloud.particles.json", true, true};
constexpr Example gpu_quads{"Scenes/243-gpu-camera-particles.scene.json",
                            "Assets/Particles/Billboards/DeviceCloud.particles.json", false, true,
                            true};
constexpr Example graph_quads{"Scenes/244-gpu-graph-camera-particles.scene.json",
                              "Assets/Particles/Billboards/GraphCloud.particles.json",
                              false,
                              true,
                              true,
                              true};
constexpr Example ribbons{"Scenes/245-analytic-particle-trails.scene.json",
                          "Assets/Particles/Trails/RibbonFlow.particles.json",
                          true,
                          false,
                          false,
                          false,
                          true};
constexpr Example colliding{"Scenes/246-particle-plane-collision.scene.json",
                            "Assets/Particles/Collision/BouncingCloud.particles.json",
                            true,
                            false,
                            false,
                            false,
                            false,
                            true};
constexpr Example weighted_quads{"Scenes/247-weighted-gpu-particles.scene.json",
                                 "Assets/Particles/Weighted/DeviceBlend.particles.json",
                                 false,
                                 true,
                                 true,
                                 false,
                                 false,
                                 false,
                                 true};
constexpr Example weighted_graph_quads{"Scenes/248-weighted-gpu-graph-particles.scene.json",
                                       "Assets/Particles/Weighted/GraphBlend.particles.json",
                                       false,
                                       true,
                                       true,
                                       true,
                                       false,
                                       false,
                                       true};
constexpr Example gpu_fading{"Scenes/249-gpu-particle-opacity.scene.json",
                             "Assets/Particles/Opacity/DeviceFade.particles.json",
                             true,
                             true,
                             true,
                             false,
                             false,
                             false,
                             true,
                             true};
constexpr Example graph_fading{"Scenes/250-gpu-graph-particle-opacity.scene.json",
                               "Assets/Particles/Opacity/GraphFade.particles.json",
                               true,
                               true,
                               true,
                               true,
                               false,
                               false,
                               true,
                               true};
constexpr Example gpu_colliding{"Scenes/251-gpu-particle-plane-collision.scene.json",
                                "Assets/Particles/Collision/DeviceBounces.particles.json",
                                true,
                                false,
                                true,
                                false,
                                false,
                                true,
                                true,
                                true};
constexpr Example gpu_ribbons{.scene = "Scenes/252-gpu-analytic-particle-trails.scene.json",
                              .effect = "Assets/Particles/Trails/DeviceRibbonFlow.particles.json",
                              .opacity = true,
                              .gpu = true,
                              .trails = true,
                              .weighted = true,
                              .gpu_opacity = true};
constexpr Example gpu_sorted{.scene = "Scenes/253-ordered-gpu-particles.scene.json",
                             .effect = "Assets/Particles/Alpha/OrderedDeviceCloud.particles.json",
                             .opacity = true,
                             .billboards = true,
                             .gpu = true,
                             .gpu_opacity = true,
                             .sorted = true};
constexpr const char* trail_material = "Assets/Particles/Trails/Ribbon.material.json";
constexpr Example scene_colliding{.scene = "Scenes/254-scene-particle-collision.scene.json",
                                  .effect =
                                      "Assets/Particles/Collision/SceneBounces.particles.json",
                                  .opacity = true,
                                  .billboards = true,
                                  .scene_collision = true};
constexpr Example xy_scene_colliding{.scene = "Scenes/258-xy-scene-particle-collision.scene.json",
                                     .effect =
                                         "Assets/Particles/Collision/XYSceneBounces.particles.json",
                                     .opacity = true,
                                     .billboards = true,
                                     .scene_collision = true,
                                     .scene_collision_2d = true};
constexpr const char* alpha_material = "Assets/Particles/Alpha/Glass.material.json";
constexpr const char* trail_animation = "Assets/Particles/Trails/EmitterSweep.animation.json";
constexpr Example world_ribbons{.scene = "Scenes/259-world-particle-trails.scene.json",
                                .effect = "Assets/Particles/Trails/WorldFlow.particles.json",
                                .opacity = true,
                                .scene_collision = true,
                                .world_trails = true};
constexpr Example xy_world_ribbons{.scene = "Scenes/260-xy-world-particle-trails.scene.json",
                                   .effect = "Assets/Particles/Trails/XYWorldFlow.particles.json",
                                   .opacity = true,
                                   .scene_collision = true,
                                   .scene_collision_2d = true,
                                   .world_trails = true};
constexpr Example scene_impacts{.scene = "Scenes/261-scene-particle-impacts.scene.json",
                                .effect = "Assets/Particles/Collision/SceneImpacts.particles.json",
                                .opacity = true,
                                .scene_collision = true,
                                .impacts = true};
constexpr Example xy_scene_impacts{.scene = "Scenes/262-xy-particle-impact-trails.scene.json",
                                   .effect = "Assets/Particles/Trails/XYImpactFlow.particles.json",
                                   .opacity = true,
                                   .scene_collision = true,
                                   .scene_collision_2d = true,
                                   .world_trails = true,
                                   .impacts = true};
constexpr Example scene_resting{.scene = "Scenes/263-resting-scene-particles.scene.json",
                                .effect = "Assets/Particles/Collision/RestingCloud.particles.json",
                                .opacity = true,
                                .scene_collision = true,
                                .impacts = true,
                                .resting = true};
constexpr Example xy_scene_resting{.scene = "Scenes/264-xy-resting-scene-particles.scene.json",
                                   .effect =
                                       "Assets/Particles/Collision/XYRestingCloud.particles.json",
                                   .opacity = true,
                                   .billboards = true,
                                   .scene_collision = true,
                                   .scene_collision_2d = true,
                                   .impacts = true,
                                   .resting = true};
constexpr const char* quad_material = "Assets/Particles/Billboards/SoftCloud.material.json";
constexpr const char* quad_texture = "Assets/Particles/Billboards/SoftCloud.paint-texture.json";
constexpr const char* gpu_quad_material = "Assets/Particles/Billboards/DeviceCloud.material.json";
constexpr const char* graph_effect = "Assets/Particles/Graphs/OrbitFountain.particles.json";
const char* material_source(const Example& example) {
    return example.collision                          ? alpha_material
           : (example.trails || example.world_trails) ? trail_material
           : (example.weighted || example.sorted)     ? quad_material
           : example.gpu                              ? gpu_quad_material
           : example.billboards                       ? quad_material
                                                      : alpha_material;
}
unsigned value_version(const Example& example) {
    if (example.resting)
        return example.scene_collision_2d ? 71u : 68u;
    if (example.impacts)
        return example.world_trails         ? (example.scene_collision_2d ? 61u : 60u)
               : example.scene_collision_2d ? (example.billboards ? 59u : 58u)
                                            : (example.billboards ? 57u : 56u);
    return example.world_trails               ? (example.scene_collision_2d ? 55u : 54u)
           : example.scene_collision_2d       ? 53u
           : example.scene_collision          ? 39u
           : example.sorted                   ? 34u
           : example.gpu && example.trails    ? (example.gpu_opacity ? 25u
                                                 : example.weighted  ? 24u
                                                                     : 23u)
           : example.gpu && example.collision ? (example.gpu_opacity ? 22u
                                                 : example.weighted  ? 21u
                                                                     : 20u)
           : example.gpu_opacity              ? (example.graph ? 19u : 18u)
           : example.weighted                 ? (example.graph ? 15u : 14u)
           : example.collision                ? 11u
           : example.trails                   ? 10u
           : example.graph                    ? 9u
           : example.gpu                      ? 8u
           : example.billboards               ? 7u
           : example.opacity                  ? 6u
                                              : 5u;
}
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void write(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    asset_storage::replace(path, value.dump(2) + "\n");
}
RuntimePackageTarget target() {
#ifdef _WIN32
    return {"windows", "d3d12"};
#else
    return {"linux", "none"};
#endif
}
void author(const std::filesystem::path& root, const Example& example) {
    const auto* material_path = material_source(example);
    const bool reuse_material = example.scene_collision || example.weighted || example.sorted ||
                                example.graph ||
                                (example.opacity && !example.billboards && !example.trails);
    for (const auto* path : {example.scene, example.effect})
        check(!std::filesystem::exists(root / path), "Sorted-alpha Gallery source already exists");
    check(reuse_material == std::filesystem::exists(root / material_path),
          "Shared particle Material must exist exactly when requested");
    auto material = reuse_material ? MaterialSource::parse(asset_detail::read_bytes(
                                         root / material_path, material_source_byte_limit))
                                   : MaterialSource::create(AssetId::generate());
    if (!reuse_material) {
        auto& overrides = material.document["overrides"];
        overrides["model"] = "forge.gltf.unlit.v1";
        overrides["alpha"] =
            unsigned(example.gpu && !example.weighted && !example.sorted ? MaterialAlpha::Mask
                                                                         : MaterialAlpha::Blend);
        overrides["depth_test"] = true;
        overrides["depth_write"] = example.gpu && !example.weighted && !example.sorted;
        if (example.trails)
            overrides["double_sided"] = true;
        overrides["parameters"]["baseColorFactor"] = {
            {"type", unsigned(MaterialParameterType::LinearColor4)}, {"value", {1, 1, 1, .4}}};
        if (example.billboards) {
            AssetId texture_id;
            if (example.gpu) {
                const auto texture = PaintTextureSource::parse(
                    asset_detail::read_bytes(root / quad_texture, paint_texture_source_byte_limit));
                texture_id = texture.asset();
            } else {
                texture_id = AssetId::generate();
                Rgba8Pixels pixels;
                pixels.width = pixels.height = 32;
                pixels.rgba.resize(32 * 32 * 4);
                for (unsigned y = 0; y < 32; ++y)
                    for (unsigned x = 0; x < 32; ++x) {
                        const double px = (double(x) + .5 - 16) / 15;
                        const double py = (double(y) + .5 - 16) / 15;
                        const auto index = (y * 32 + x) * 4;
                        pixels.rgba[index] = std::uint8_t(128 + 127 * double(x) / 31);
                        pixels.rgba[index + 1] = std::uint8_t(128 + 127 * double(y) / 31);
                        pixels.rgba[index + 2] = 255;
                        pixels.rgba[index + 3] = std::uint8_t(
                            std::lround(255 * std::clamp((1 - px * px - py * py) * 2, 0., 1.)));
                    }
                const auto texture =
                    PaintTextureValue::create(texture_id, 32, 32).replace_pixels(pixels);
                std::filesystem::create_directories((root / quad_texture).parent_path());
                asset_storage::replace(root / quad_texture, texture.dump(2) + "\n");
            }
            MaterialData shape;
            shape.model = "forge.gltf.unlit.v1";
            auto& slot = shape.textures["baseColorTexture"];
            slot.semantic = TextureSemantic::Color;
            slot.sampler.u = slot.sampler.v = TextureWrap::ClampEdge;
            overrides["double_sided"] = false;
            overrides["textures"]["baseColorTexture"] = {
                {"asset", texture_id},
                {"slot", material_values_document(shape).at("textures").at("baseColorTexture")}};
            overrides["parameters"]["baseColorFactor"]["value"] = {1, 1, 1, example.gpu ? 1. : .7};
        }
        material.validate();
        write(root / material_path, material.document);
    }
    auto effect = ParticleEffectSource::create(AssetId::generate());
    effect.set_evaluation(example.gpu ? ParticleEvaluation::Gpu : ParticleEvaluation::CpuAlpha);
    effect.document["material"] = AssetRef<MaterialAsset>{material.asset()};
    effect.document["capacity"] = 128;
    effect.document["spawn_rate"] = 32;
    effect.document["lifetime"] = 4;
    effect.document["shape"] = "box";
    effect.document["shape_size"] = {2.5, 1.5, 2};
    effect.document["speed_min"] = .2;
    effect.document["speed_max"] = .5;
    effect.document["acceleration"] = {0, 0, 0};
    effect.document["size_start"] = .35;
    effect.document["size_end"] = .1;
    effect.document["color_start"] = effect.document["color_end"] = {1, 1, 1};
    if (example.opacity) {
        effect.enable_opacity(true);
        effect.document["opacity_start"] = effect.document["opacity_end"] = 0;
        effect.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
        effect.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .15);
        effect.document["opacity_curve"][1]["value"] = 1;
    }
    if (example.trails) {
        effect.document["spawn_rate"] = 8;
        effect.document["lifetime"] = 2.5;
        effect.document["shape"] = "disc";
        effect.document["shape_radius"] = .2;
        effect.document["cone_angle"] = 35;
        effect.document["speed_min"] = 3;
        effect.document["speed_max"] = 4;
        effect.document["acceleration"] = {0, -3.2, 1.2};
        effect.document["drag"] = .15;
        effect.document["size_start"] = .16;
        effect.document["size_end"] = .03;
        if (example.gpu)
            effect.set_gpu_trails(true, ParticleTrailSettings{.75, 8});
        else
            effect.set_trails(true, ParticleTrailSettings{.75, 8});
    }
    if (example.collision || example.scene_collision) {
        effect.document["spawn_rate"] = 24;
        effect.document["shape_size"] = {2, .25, 1};
        effect.document["cone_angle"] = 35;
        effect.document["speed_min"] = .5;
        effect.document["speed_max"] = 1.5;
        effect.document["acceleration"] = {0, -4, 0};
        effect.document["drag"] = .1;
        effect.document["size_start"] = .12;
        effect.document["size_end"] = .06;
        const ParticleCollisionPlane plane{{0, 1, 0}, -1, .06, .65, .15, 2};
        if (example.scene_collision)
            effect.set_scene_collision(true, ParticleSceneCollision{.06, .65, .15, 2, UINT32_MAX},
                                       example.scene_collision_2d);
        else if (example.gpu)
            effect.set_gpu_collision(true, plane);
        else
            effect.set_collision(true, plane);
    }
    if (example.graph) {
        const auto retained = ParticleEffectSource::parse(
            asset_detail::read_bytes(root / graph_effect, particle_effect_byte_limit));
        check(bool(retained.graph_program()), "Graph camera example requires its retained program");
        effect.set_graph_program(*retained.graph_program(), true);
    }
    if (example.gpu && !example.collision && !example.trails)
        effect.enable_gpu_billboards(true);
    else if (example.billboards)
        effect.enable_billboards(true);
    if (example.weighted)
        effect.enable_gpu_weighted_alpha(true);
    if (example.sorted)
        effect.enable_gpu_sorted_alpha(true);
    if (example.gpu_opacity)
        effect.enable_gpu_opacity(true);
    effect.validate();
    if (example.world_trails)
        effect.set_world_trails(true, ParticleTrailSettings{.75, 12});
    if (example.impacts)
        effect.set_scene_impacts(true);
    if (example.resting) {
        effect.document["spawn_rate"] = 12;
        effect.set_scene_collision(true, ParticleSceneCollision{.06, 0, 1, 8, UINT32_MAX});
        effect.set_scene_resting(true);
    }
    write(root / example.effect, effect.document);
    auto catalog = AssetCatalog::open_project(root);
    if (!reuse_material)
        catalog.add({material.asset(), MaterialAsset::type, material_path, 1, {}});
    if (example.billboards && !example.gpu && !reuse_material) {
        const auto texture = PaintTextureSource::parse(
            asset_detail::read_bytes(root / quad_texture, paint_texture_source_byte_limit));
        catalog.add({texture.asset(), TextureAsset::type, quad_texture, 1, {}});
    }
    catalog.add({effect.asset(), ParticleEffectAsset::type, example.effect, 1, {}});
    AssetId motion;
    if (example.world_trails) {
        TransformAnimationSource clip;
        if (std::filesystem::exists(root / trail_animation))
            clip = TransformAnimationSource::parse(
                asset_detail::read_bytes(root / trail_animation, transform_animation_byte_limit));
        else {
            clip = TransformAnimationSource::create(AssetId::generate());
            clip.document["duration_us"] = 4000000;
            clip.document["channels"] = {"forge.local_translation"};
            clip.document["keys"] = Json::array();
            for (unsigned i = 0; i < 3; ++i)
                clip.document["keys"].push_back({{"id", AssetId::generate()},
                                                 {"time_us", i * 2000000},
                                                 {"translation", {i == 1 ? 2 : -2, 2.5, .5}},
                                                 {"rotation", {0, 0, 0, 1}},
                                                 {"scale", {1, 1, 1}},
                                                 {"interpolation", "linear"}});
            clip.validate();
            write(root / trail_animation, clip.document);
            catalog.add({clip.asset(), TransformAnimationAsset::type, trail_animation, 1, {}});
        }
        motion = clip.asset();
    }
    catalog.save(AssetCatalog::project_index(root));
    EngineContext engine;
    Scene scene(engine.world());
    auto actor = [&](const char* name) {
        auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                       .at("selected")
                                       .get<std::string>());
        entity.set<Primitive>({no_primitive});
        return entity;
    };
    auto teal = actor(example.scene_collision ? "Teal Scene-collision particles"
                      : example.collision     ? "Teal bouncing plane particles"
                      : example.trails        ? "Teal analytic ribbon trails"
                      : example.gpu           ? "Teal camera quads - shared device effect"
                      : example.opacity       ? "Teal fade - shared opacity effect"
                                              : "Teal glass - shared sorted-alpha effect");
    teal.set<LocalTranslation>({-.7f, example.scene_collision ? 2.5 : 1, .5f});
    ParticleEmitter emitter;
    emitter.effect = {effect.asset()};
    emitter.seed = 41;
    emitter.tint_r = .15f;
    emitter.tint_g = .8f;
    teal.set<ParticleEmitter>(emitter);
    if (example.world_trails) {
        TransformAnimator animator;
        animator.clip = {motion};
        teal.set<TransformAnimator>(animator);
        teal.set<LocalTranslation>({-2, 2.5, .5});
    }
    if (example.billboards)
        teal.set<LocalScale>({1.4f, 1, 0});
    auto amber = actor(example.scene_collision ? "Amber Scene-collision particles"
                       : example.collision     ? "Amber bouncing plane particles"
                       : example.trails        ? "Amber analytic ribbon trails"
                       : example.gpu           ? "Amber camera quads - shared device effect"
                       : example.opacity       ? "Amber fade - shared opacity effect"
                                               : "Amber glass - shared sorted-alpha effect");
    amber.set<LocalTranslation>({.7f, example.scene_collision ? 2.5 : 1, -.5f});
    emitter.seed = 71;
    emitter.tint_r = 1;
    emitter.tint_g = .4f;
    emitter.tint_b = .1f;
    amber.set<ParticleEmitter>(emitter);
    if (example.billboards)
        amber.set<LocalScale>({-.8f, 1.3f, 1});
    auto marker = actor(example.scene_collision ? "Floor - move or resize before Play"
                        : example.collision     ? "Collision plane visual guide"
                                                : "Opaque depth marker");
    MeshRenderer mesh;
    mesh.mesh = engine_primitive(0);
    marker.set<MeshRenderer>(mesh);
    marker.set<LocalTranslation>((example.collision || example.scene_collision)
                                     ? LocalTranslation{0, -.05f, 0}
                                     : LocalTranslation{0, 1, -.75f});
    marker.set<LocalScale>(example.scene_collision ? LocalScale{12, .1f, 10}
                           : example.collision     ? LocalScale{7, .1f, 5}
                                                   : LocalScale{.2f, 2, .2f});
    if (example.scene_collision) {
        if (example.scene_collision_2d) {
            marker.set<PhysicsBody2D>({});
            marker.set<BoxCollider2D>({1, 1});
        } else {
            marker.set<PhysicsBody>({});
            marker.set<BoxCollider>({1, 1, 1});
        }
        auto obstacle = actor("Inclined obstacle - rotate before Play");
        obstacle.set<MeshRenderer>(mesh);
        obstacle.set<LocalTranslation>({0, .5, 0});
        obstacle.set<LocalScale>({2, .25f, 2});
        obstacle.set<LocalRotation>(rotation_about_axis({0, 0, 1}, 25));
        if (example.scene_collision_2d) {
            obstacle.set<PhysicsBody2D>({});
            obstacle.set<BoxCollider2D>({1, 1});
        } else {
            obstacle.set<PhysicsBody>({});
            obstacle.set<BoxCollider>({1, 1, 1});
        }
    }
    auto camera = actor("Game Camera");
    camera.set<Camera>({});
    camera.set<LocalTranslation>({0, 2, -9});
    auto light = actor("Directional light");
    light.set<Light>({});
    light.set<LocalRotation>({.3826834261f, 0, 0, .9238795042f});
    if (example.weighted)
        authoring_command(scene, "scene.rendering.set", {{"weighted_transparency", true}});
    scene.save(root / example.scene);
    catalog = AssetCatalog::open_project(root);
    catalog.add_scene(example.scene);
    catalog.save(AssetCatalog::project_index(root));
}
std::vector<AssetId> inspect(const std::filesystem::path& root, const Example& example) {
    const auto* material_path = material_source(example);
    const auto material = MaterialSource::parse(
        asset_detail::read_bytes(root / material_path, material_source_byte_limit));
    const auto resolved = resolve_material_source(material);
    check(resolved.values.model == "forge.gltf.unlit.v1" &&
              resolved.values.alpha == (example.gpu && !example.weighted && !example.sorted
                                            ? MaterialAlpha::Mask
                                            : MaterialAlpha::Blend) &&
              resolved.values.depth_test &&
              resolved.values.depth_write == (example.gpu && !example.weighted && !example.sorted),
          "Particle example lost its reusable admitted Material");
    const auto effect = ParticleEffectSource::parse(
        asset_detail::read_bytes(root / example.effect, particle_effect_byte_limit));
    check(effect.compiled().version == value_version(example) &&
              effect.compiled().material.id == material.asset() &&
              bool(effect.compiled().program) == example.graph && effect.compiled().capacity == 128,
          "Particle example lost its evaluation profile or shared Material");
    if (example.trails)
        check(resolved.values.double_sided &&
                  effect.document.at("version").get<unsigned>() == (example.gpu ? 14u : 9u) &&
                  effect.document.at("mesh").get<AssetRef<MeshAsset>>() == engine_primitive(0) &&
                  effect.compiled().mesh == engine_primitive(8) &&
                  effect.compiled().trail == ParticleTrailSettings{.75, 8} &&
                  particle_instance_capacity(effect.compiled()) == 1024,
              "Trail example lost joined geometry, retained Mesh, settings or segment reservation");
    if (example.collision)
        check(effect.document.at("version").get<unsigned>() == (example.gpu ? 13u : 10u) &&
                  effect.compiled().collision ==
                      ParticleCollisionPlane{{0, 1, 0}, -1, .06, .65, .15, 2} &&
                  effect.compiled().mesh == engine_primitive(0),
              "Collision example lost its typed plane, bounded response or ordinary Mesh");
    if (example.opacity)
        check(effect.compiled().opacity_curve.size() == 3 &&
                  effect.compiled().opacity_curve[0].value == 0 &&
                  effect.compiled().opacity_curve[1].time == .15 &&
                  effect.compiled().opacity_curve[1].value == 1 &&
                  effect.compiled().opacity_curve[2].value == 0,
              "Fading example lost its editable birth/fade/death opacity curve");
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / example.scene);
    const auto render = extract_render_scene(scene.effective_document());
    check(render.diagnostics.empty() && render.particles.size() == 2 &&
              render.meshes.size() == (example.scene_collision ? 2u : 1u) &&
              render.cameras.size() == 1 && render.lights.size() == 1,
          "Sorted-alpha example lost its native Scene consumers");
    if (example.scene_collision) {
        check(effect.document.at("version").get<unsigned>() == (example.resting        ? 21u
                                                                : example.impacts      ? 20u
                                                                : example.world_trails ? 19
                                                                : example.scene_collision_2d
                                                                    ? 18
                                                                    : 16) &&
                  effect.compiled().scene_collision ==
                      (example.resting ? ParticleSceneCollision{.06, 0, 1, 8, UINT32_MAX}
                                       : ParticleSceneCollision{.06, .65, .15, 2, UINT32_MAX}) &&
                  !effect.compiled().collision && scene.document().at("entities").size() == 6,
              "Scene example lost explicit World collision settings or its six native actors");
        for (const auto& mesh : render.meshes) {
            auto entity = scene.entity(mesh.entity.str());
            check(example.scene_collision_2d
                      ? entity.has<PhysicsBody2D>() && entity.has<BoxCollider2D>() &&
                            !entity.has<PhysicsBody>()
                      : entity.has<PhysicsBody>() && entity.has<BoxCollider>(),
                  "Scene collision example contains a visual obstacle without realized collider "
                  "intent");
        }
    }
    if (example.sorted)
        check(!render.settings.weighted_transparency && effect.document.at("version") == 15 &&
                  effect.document.at("gpu_sorted_enabled") == true &&
                  !effect.document.at("gpu_weighted_enabled").get<bool>() &&
                  particle_uses_gpu_sorted_alpha(effect.compiled()),
              "Ordered Gallery source lost explicit source/effect/Scene admission");
    if (example.weighted)
        check(render.settings.weighted_transparency &&
                  effect.document.at("version").get<unsigned>() == (example.trails        ? 14u
                                                                    : example.collision   ? 13u
                                                                    : example.gpu_opacity ? 12u
                                                                                          : 11u) &&
                  effect.document.at("gpu_weighted_enabled") == true,
              "Weighted Gallery source lost explicit effect or Scene admission");
    if (example.gpu_opacity)
        check(effect.document.at("gpu_opacity_enabled") == true &&
                  particle_uses_gpu_opacity(effect.compiled()),
              "GPU fading example lost explicit per-birth opacity admission");
    for (const auto& emitter : render.particles)
        check(emitter.emitter.effect.id == effect.asset(), "Example duplicated an effect");
    const auto catalog = AssetCatalog::open_project(root);
    check(catalog.records().at(material.asset()).source == material_path &&
              catalog.records().at(effect.asset()).source == example.effect &&
              catalog.records().at(scene.asset_id()).source == example.scene,
          "Sorted-alpha example identity/catalog mismatch");
    std::vector<AssetId> ids{scene.asset_id(), effect.asset(), material.asset()};
    if (example.world_trails) {
        const auto clip = TransformAnimationSource::parse(
            asset_detail::read_bytes(root / trail_animation, transform_animation_byte_limit));
        check(catalog.records().at(clip.asset()).source == trail_animation &&
                  effect.compiled().trail == ParticleTrailSettings{.75, 12} &&
                  resolved.values.double_sided &&
                  particle_instance_capacity(effect.compiled()) == 1536,
              "Historical trail Gallery lost its animation, joined geometry or reservation");
        ids.push_back(clip.asset());
    }
    if (example.billboards) {
        const auto texture = PaintTextureSource::parse(
            asset_detail::read_bytes(root / quad_texture, paint_texture_source_byte_limit));
        check(!resolved.values.double_sided && effect.compiled().mesh == engine_primitive(8) &&
                  resolved.textures.at("baseColorTexture").id == texture.asset() &&
                  catalog.records().at(texture.asset()).source == quad_texture,
              "Camera-plane example lost its single-sided textured Quad closure");
        ids.push_back(texture.asset());
    }
    if (example.graph) {
        const auto shader = effect.compiled().program->shader.id;
        check(catalog.records().at(shader).type == ShaderAsset::type,
              "Graph camera example lost its typed Shader dependency");
        ids.push_back(shader);
    }
    return ids;
}
void prepare(const std::filesystem::path& root, const Example& example,
             bool publish_material = true, const std::filesystem::path& worker = {},
             bool publish_texture = true) {
    const auto* material_path = material_source(example);
    const auto ids = inspect(root, example);
    const auto runtime = target();
    check(!example.graph || runtime.platform == "windows",
          "Particle graph publication requires the native Windows Shader cook");
    const ImportTarget import{runtime.platform, runtime.backend,
                              runtime.platform == "windows" ? "desktop" : "cpu"};
    auto lease = std::make_shared<ProjectLease>(root);
    if (example.billboards && publish_texture) {
        check(!worker.empty(), "Textured camera-plane publication requires its asset worker");
#ifdef FORGE_GALLERY_TEXTURE_COOK
        AssetImportService textures(lease, texture_import_registry(worker), import);
        test::publish_gallery_asset(textures, quad_texture, prepare_texture_publication);
#else
        throw std::runtime_error("Textured Gallery publication requires asset tools");
#endif
    }
    AssetImportService materials(lease, material_import_registry(), import);
    if (publish_material)
        test::publish_gallery_asset(materials, material_path, prepare_material_publication);
    AssetImportService particles(lease, particle_effect_import_registry(), import);
    test::publish_gallery_asset(particles, example.effect, prepare_particle_effect_publication);
    if (example.world_trails) {
        AssetImportService animations(lease, transform_animation_import_registry(), import);
        test::publish_gallery_asset(animations, trail_animation,
                                    prepare_transform_animation_publication);
    }
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(root / example.scene);
    const std::array roots{ids[0]};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void verify_sorted_countercases(const std::filesystem::path& root,
                                const std::filesystem::path& worker) {
    prepare(root, gpu_sorted, true, worker);
    const auto ids = inspect(root, gpu_sorted);
    std::vector<AssetId> roots{ids[0]};
    {
        auto lease = std::make_shared<ProjectLease>(root);
        const auto runtime = target();
        const ImportTarget import{runtime.platform, runtime.backend,
                                  runtime.platform == "windows" ? "desktop" : "cpu"};
        AssetImportService materials(lease, material_import_registry(), import);
        AssetImportService effects(lease, particle_effect_import_registry(), import);
        const auto base_material = MaterialSource::parse(
            asset_detail::read_bytes(root / quad_material, material_source_byte_limit));
        const auto base_effect = ParticleEffectSource::parse(
            asset_detail::read_bytes(root / gpu_sorted.effect, particle_effect_byte_limit));
        std::filesystem::create_directories(root / "Assets/ParticleFixtures");
        const auto material_copy = [&](const char* name, bool double_sided) {
            const auto path = std::filesystem::path("Assets/ParticleFixtures") / name;
            auto document = MaterialDocument::create(lease, path);
            const auto id = document->source().asset();
            document->edit_source(document->revision(), "Fixture Material", [&](auto& source) {
                source = base_material;
                source.document["asset_id"] = id;
                if (double_sided)
                    source.document["overrides"]["double_sided"] = true;
            });
            document->save();
            test::publish_gallery_asset(materials, path, prepare_material_publication,
                                        std::chrono::seconds(90), id);
            return AssetRef<MaterialAsset>{id};
        };
        const auto twin = material_copy("SortedTwin.material.json", false);
        const auto two_sided = material_copy("SortedTwoSided.material.json", true);
        const auto effect_copy = [&](const char* name, const auto& edit) {
            const auto path = std::filesystem::path("Assets/ParticleFixtures") / name;
            auto document = ParticleEffectDocument::create(lease, path);
            const auto id = document->source().asset();
            document->edit_source(document->revision(), "Fixture Effect", [&](auto& source) {
                source = base_effect;
                source.document["asset_id"] = id;
                edit(source);
            });
            document->save();
            test::publish_gallery_asset(effects, path, prepare_particle_effect_publication,
                                        std::chrono::seconds(90), id);
            roots.push_back(id);
        };
        effect_copy("SortedCompatible.particles.json", [](auto&) {});
        effect_copy("SortedDifferent.particles.json",
                    [&](auto& source) { source.document["material"] = twin; });
        effect_copy("SortedCpu.particles.json",
                    [](auto& source) { source.set_evaluation(ParticleEvaluation::CpuAlpha); });
        effect_copy("SortedWeighted.particles.json",
                    [](auto& source) { source.enable_gpu_weighted_alpha(true); });
        effect_copy("SortedPlane.particles.json", [&](auto& source) {
            source.document["material"] = two_sided;
            source.set_gpu_collision(true);
        });
        effect_copy("SortedRibbon.particles.json", [&](auto& source) {
            source.document["material"] = two_sided;
            source.document["spawn_rate"] = 24;
            source.set_gpu_trails(true, ParticleTrailSettings{.75, 4});
        });
#ifdef _WIN32
        const auto graph = ParticleEffectSource::parse(
            asset_detail::read_bytes(root / graph_effect, particle_effect_byte_limit));
        check(bool(graph.graph_program()), "Published countercases require the native Graph cook");
        effect_copy("SortedGraph.particles.json",
                    [&](auto& source) { source.set_graph_program(*graph.graph_program(), true); });
#endif
        EngineContext engine;
        Scene scene(engine.world());
        scene.load(root / gpu_sorted.scene);
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    }
    const auto output = root.parent_path() / "content";
    const auto relocated = root.parent_path() / "relocated-content";
    (void)package_runtime_content(root, output, roots, target());
    std::filesystem::rename(root, root.parent_path() / "source-offline");
    std::filesystem::rename(output, relocated);
    const auto catalog = open_runtime_content(relocated, target());
    check(!std::filesystem::exists(relocated / gpu_sorted.effect) &&
              !std::filesystem::exists(relocated /
                                       "Assets/ParticleFixtures/SortedDifferent.particles.json"),
          "Countercase export retained editable Effect sources");
    std::cout << "Published source-hidden ordered countercases; roots=" << roots.size()
              << " records=" << catalog.records().size() << '\n';
}
void verify_export(const std::filesystem::path& root, const Example& example,
                   const std::filesystem::path& worker = {}) {
    const auto* material_path = material_source(example);
    const auto ids = inspect(root, example);
    prepare(root, example, true, worker);
    const auto output = root.parent_path() / "content";
    const auto relocated = root.parent_path() / "relocated-content";
    const std::array roots{ids[0]};
    (void)package_runtime_content(root, output, roots, target());
    std::filesystem::rename(root, root.parent_path() / "source-offline");
    std::filesystem::rename(output, relocated);
    const auto catalog = open_runtime_content(relocated, target());
    std::set<AssetId> actual;
    for (const auto& [id, record] : catalog.records()) {
        (void)record;
        actual.insert(id);
    }
    check(actual == std::set<AssetId>(ids.begin(), ids.end()) &&
              !std::filesystem::exists(relocated / material_path) &&
              !std::filesystem::exists(relocated / example.effect) &&
              (!example.billboards || !std::filesystem::exists(relocated / quad_texture)),
          "Sorted-alpha relocation lost its exact source-hidden closure");
    ResourcePool<ParticleEffectAsset> effects;
    const auto ticket = request_particle_effect(
        effects, relocated, std::make_shared<const AssetCatalog>(catalog), {ids[1]});
    check(effects.wait(ticket, std::chrono::seconds(15)), "Relocated alpha effect load timed out");
    const auto selected = effects.acquire(ticket);
    check(bool(selected) && selected->version == value_version(example) &&
              selected->material.id == ids[2],
          "Relocated source-free alpha effect has different values");
    const auto material = asset_detail::load_material_selection(relocated, catalog, {ids[2]});
    check(material.data.values.alpha == (example.gpu && !example.weighted && !example.sorted
                                             ? MaterialAlpha::Mask
                                             : MaterialAlpha::Blend) &&
              material.data.values.depth_write ==
                  (example.gpu && !example.weighted && !example.sorted),
          "Relocated source-free particle Material has different values");
    std::vector<EngineModule> modules{particle_module(relocated)};
    if (example.world_trails)
        modules.push_back(animation_module(relocated));
    if (example.scene_collision)
        modules.push_back(physics_module());
    EngineContext runtime(WorldRole::Runtime, false, std::move(modules));
    Scene scene(runtime.world());
    scene.load(relocated / catalog.records().at(ids[0]).source);
    const auto saved = scene.document();
    RuntimeSimulation simulation(runtime.world(), scene);
    if (example.world_trails) {
        const auto animation = animation_runtime(runtime.world());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Historical trail animation adoption stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
    }
    const auto particles = particle_runtime(runtime.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!particles->prepare()) {
        check(std::chrono::steady_clock::now() < deadline, "Source-free alpha World timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    simulation.reset_presentation();
    for (unsigned i = 0; i < 12; ++i)
        simulation.tick(.25f);
    const auto render = extract_render_scene(simulation.presentation(1));
    auto observed = scene.document();
    if (example.world_trails) {
        for (std::size_t i = 0; i < observed.at("entities").size(); ++i) {
            auto& components = observed["entities"][i]["components"];
            if (components.contains("forge.transform_animator")) {
                const auto& position = components.at("forge.local_translation");
                check(std::abs(position.at("x").get<double>()) < 1e-5 &&
                          position.at("y").get<double>() == 2.5 &&
                          position.at("z").get<double>() == .5,
                      "Source-hidden World trail emitter lost its authored looping translation");
                components["forge.local_translation"] =
                    saved.at("entities").at(i).at("components").at("forge.local_translation");
            }
        }
    }
    check(render.diagnostics.empty() && render.particles.size() == 2 && observed == saved,
          "Source-free alpha World lost its compact emitters or mutated authored values");
    if (example.sorted)
        check(!render.settings.weighted_transparency &&
                  particle_uses_gpu_sorted_alpha(selected.get()),
              "Relocated ordered Game lost explicit effect or Scene admission");
    if (example.weighted)
        check(render.settings.weighted_transparency && particle_uses_weighted_alpha(selected.get()),
              "Relocated weighted Game lost explicit effect or Scene admission");
    for (const auto& emitter : render.particles) {
        if (example.gpu) {
            bool refused = false;
            try {
                (void)particles->sample(scene.entity(emitter.entity.str()).id(), 1);
            } catch (const std::runtime_error&) {
                refused = true;
            }
            const auto values = prepare_particle_gpu_values(
                selected.get(), emitter.time, emitter.emitter.seed, emitter.world, CameraView{},
                emitter.emitter.size_multiplier,
                {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b});
            if (example.gpu_opacity) {
                const auto opacity = prepare_particle_gpu_opacity_values(selected.get());
                check(opacity.count == 3 && opacity.endpoints == std::array<float, 2>{0, 0} &&
                          opacity.stops[1][0] == float(.15) && opacity.stops[1][1] == 1,
                      "Source-hidden GPU World lost its shared lifetime appearance packet");
            }
            if (example.collision) {
                const auto plane = prepare_particle_gpu_collision_values(*selected->collision);
                check(plane.plane == ParticleGpuRow{0, 1, 0, float(-.94)} && plane.response[2] == 2,
                      "Source-hidden GPU World lost its emitter-local plane packet");
            }
            check(refused && emitter.time == 3 &&
                      values.births[0] == (example.trails      ? 24u
                                           : example.collision ? 72u
                                                               : 96u),
                  "Source-free GPU World lost its fixed-clock device window or substituted CPU");
            continue;
        }
        const auto samples = particles->sample(scene.entity(emitter.entity.str()).id(), 1);
        check(emitter.time == 3 &&
                  (example.scene_collision ? !samples.empty() && samples.size() <= 72
                   : example.collision     ? !samples.empty() && samples.size() < 72
                                           : samples.size() == (example.trails ? 20u : 96u)),
              "Source-free alpha emitter lost fixed-clock CPU birth membership");
        if (example.scene_collision) {
            check(emitter.world_samples && emitter.world_samples->size() == samples.size(),
                  "Source-hidden Scene effect lost its copied World observations");
            if (example.resting)
                check(std::any_of(samples.begin(), samples.end(),
                                  [](const auto& particle) {
                                      return particle.velocity[1] == 0 &&
                                             particle.position[1] < .07;
                                  }),
                      "Source-hidden resting Gallery has no supported living floor head");
            if (example.world_trails) {
                check(emitter.world_paths && !emitter.world_paths->paths.paths.empty(),
                      "Source-hidden World trails lost their detached historical geometry");
                validate_particle_world_paths(selected.get(), emitter.time, *emitter.world_paths,
                                              *emitter.world_samples);
            }
            for (const auto& particle : samples)
                if (std::abs(particle.position[0]) < 5.9 && std::abs(particle.position[2]) < 4.9)
                    check(particle.position[1] >= .06 - 1e-5,
                          "Source-hidden Scene particle tunneled a live center through the floor");
        }
        if (example.collision)
            for (const auto& particle : samples)
                check(particle.position[1] >= -.94 - 1e-10,
                      "Source-hidden World tunneled a live center through its collision plane");
        if (example.trails) {
            const auto paths =
                sample_particle_paths(selected.get(), emitter.time, emitter.emitter.seed,
                                      selected->trail->duration, selected->trail->segments);
            check(paths.paths.size() == 23 && paths.segment_count() <= 1024 &&
                      paths.paths.front().birth < samples.front().birth,
                  "Source-free World lost shrinking tails or original live-head membership");
        }
        if (example.opacity) {
            const auto [minimum, maximum] = std::minmax_element(
                samples.begin(), samples.end(),
                [](const auto& a, const auto& b) { return a.opacity < b.opacity; });
            check(minimum->opacity >= 0 && minimum->opacity < .1 && maximum->opacity > .9 &&
                      maximum->opacity <= 1,
                  "Source-free World did not evaluate independent per-birth opacity");
        }
    }
    std::cout << (example.gpu
                      ? "GPU quad publication/relocation and compact fixed-clock World passed\n"
                      : "CPU sorted-alpha publication/relocation and fixed-clock World passed\n");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Need sorted-alpha Gallery action and project root");
        const auto root = std::filesystem::absolute(argv[2]);
        const std::string_view action = argv[1];
        if (action == "--author-resting" || action == "--author-xy-resting")
            author(root, action == "--author-resting" ? scene_resting : xy_scene_resting);
        else if (action == "--inspect-resting" || action == "--inspect-xy-resting")
            (void)inspect(root, action == "--inspect-resting" ? scene_resting : xy_scene_resting);
        else if (action == "--prepare-resting")
            prepare(root, scene_resting);
        else if (action == "--prepare-xy-resting") {
            check(argc == 4, "Need the texture publication worker");
            prepare(root, xy_scene_resting, true, std::filesystem::absolute(argv[3]));
        } else if (action == "--author-scene-impacts" || action == "--author-xy-scene-impacts")
            author(root, action == "--author-scene-impacts" ? scene_impacts : xy_scene_impacts);
        else if (action == "--inspect-scene-impacts" || action == "--inspect-xy-scene-impacts")
            (void)inspect(root,
                          action == "--inspect-scene-impacts" ? scene_impacts : xy_scene_impacts);
        else if (action == "--prepare-scene-impacts" || action == "--prepare-xy-scene-impacts")
            prepare(root, action == "--prepare-scene-impacts" ? scene_impacts : xy_scene_impacts);
        else if (action == "--author-world-trails")
            author(root, world_ribbons);
        else if (action == "--author-xy-world-trails")
            author(root, xy_world_ribbons);
        else if (action == "--inspect-world-trails")
            (void)inspect(root, world_ribbons);
        else if (action == "--inspect-xy-world-trails")
            (void)inspect(root, xy_world_ribbons);
        else if (action == "--author-xy-scene-collision")
            author(root, xy_scene_colliding);
        else if (action == "--inspect-xy-scene-collision")
            (void)inspect(root, xy_scene_colliding);
        else if (action == "--prepare-xy-scene-collision")
            prepare(root, xy_scene_colliding, true,
                    argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path{});
        else if (action == "--author-scene-collision")
            author(root, scene_colliding);
        else if (action == "--inspect-scene-collision")
            (void)inspect(root, scene_colliding);
        else if (action == "--prepare-scene-collision")
            prepare(root, scene_colliding, true,
                    argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path{});
        else if (action == "--author-gpu-sorted")
            author(root, gpu_sorted);
        else if (action == "--inspect-gpu-sorted")
            (void)inspect(root, gpu_sorted);
        else if (action == "--prepare-gpu-sorted")
            prepare(root, gpu_sorted, true,
                    argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path{});
        else if (action == "--author-gpu-trails")
            author(root, gpu_ribbons);
        else if (action == "--inspect-gpu-trails")
            (void)inspect(root, gpu_ribbons);
        else if (action == "--prepare-gpu-trails")
            prepare(root, gpu_ribbons);
        else if (action == "--author-gpu-collision")
            author(root, gpu_colliding);
        else if (action == "--inspect-gpu-collision")
            (void)inspect(root, gpu_colliding);
        else if (action == "--prepare-gpu-collision")
            prepare(root, gpu_colliding);
        else if (action == "--author-gpu-opacity" || action == "--author-gpu-graph-opacity")
            author(root, action == "--author-gpu-opacity" ? gpu_fading : graph_fading);
        else if (action == "--inspect-gpu-opacity" || action == "--inspect-gpu-graph-opacity")
            (void)inspect(root, action == "--inspect-gpu-opacity" ? gpu_fading : graph_fading);
        else if (action == "--prepare-gpu-opacity" || action == "--prepare-gpu-graph-opacity") {
            check(argc == 4, "Need the texture publication worker");
            prepare(root, action == "--prepare-gpu-opacity" ? gpu_fading : graph_fading, true,
                    std::filesystem::absolute(argv[3]));
        } else if (action == "--author-weighted" || action == "--author-weighted-graph")
            author(root, action == "--author-weighted" ? weighted_quads : weighted_graph_quads);
        else if (action == "--inspect-weighted" || action == "--inspect-weighted-graph")
            (void)inspect(root,
                          action == "--inspect-weighted" ? weighted_quads : weighted_graph_quads);
        else if (action == "--prepare-weighted" || action == "--prepare-weighted-graph") {
            check(argc == 4, "Need the texture publication worker");
            prepare(root, action == "--prepare-weighted" ? weighted_quads : weighted_graph_quads,
                    true, std::filesystem::absolute(argv[3]));
        } else if (action == "--author-collision")
            author(root, colliding);
        else if (action == "--inspect-collision")
            (void)inspect(root, colliding);
        else if (action == "--prepare-collision")
            prepare(root, colliding);
        else if (action == "--author-trails")
            author(root, ribbons);
        else if (action == "--inspect-trails")
            (void)inspect(root, ribbons);
        else if (action == "--prepare-trails")
            prepare(root, ribbons);
        else if (action == "--author-gpu-billboards" || action == "--author-gpu-graph-billboards")
            author(root, action == "--author-gpu-billboards" ? gpu_quads : graph_quads);
        else if (action == "--inspect-gpu-billboards" || action == "--inspect-gpu-graph-billboards")
            (void)inspect(root, action == "--inspect-gpu-billboards" ? gpu_quads : graph_quads);
        else if (action == "--prepare-gpu-billboards" ||
                 action == "--prepare-gpu-graph-billboards") {
            check(argc == 4, "Need the texture publication worker");
            prepare(root, action == "--prepare-gpu-billboards" ? gpu_quads : graph_quads, true,
                    std::filesystem::absolute(argv[3]));
        } else if (action == "--author-billboards")
            author(root, quads);
        else if (action == "--inspect-billboards")
            (void)inspect(root, quads);
        else if (action == "--prepare-billboards") {
            check(argc == 4, "Need the texture publication worker");
            prepare(root, quads, true, std::filesystem::absolute(argv[3]));
        } else if (action == "--author" || action == "--author-opacity")
            author(root, action == "--author" ? alpha : fading);
        else if (action == "--inspect" || action == "--inspect-opacity")
            (void)inspect(root, action == "--inspect" ? alpha : fading);
        else if (action == "--prepare") {
            prepare(root, alpha);
            if (std::filesystem::exists(root / fading.effect))
                prepare(root, fading, false);
            if (std::filesystem::exists(root / ribbons.effect))
                prepare(root, ribbons);
            if (std::filesystem::exists(root / colliding.effect))
                prepare(root, colliding, false);
            if (std::filesystem::exists(root / gpu_colliding.effect))
                prepare(root, gpu_colliding, false);
            if (std::filesystem::exists(root / gpu_ribbons.effect))
                prepare(root, gpu_ribbons, false);
            for (const auto& example : {world_ribbons, xy_world_ribbons})
                if (std::filesystem::exists(root / example.effect))
                    prepare(root, example);
            bool texture_prepared = false;
            for (const auto& example :
                 {quads, gpu_quads, graph_quads, weighted_quads, weighted_graph_quads, gpu_fading,
                  graph_fading, gpu_sorted, scene_colliding, xy_scene_colliding}) {
#ifndef _WIN32
                // Source inspection is portable; program publication uses the native cook.
                if (example.graph)
                    continue;
#endif
                if (!std::filesystem::exists(root / example.effect))
                    continue;
                check(argc == 4, "Need the matching worker directory for textured particles");
#ifdef _WIN32
                const auto worker =
                    std::filesystem::absolute(argv[3]).parent_path() / "forge_asset_build.exe";
#else
                const auto worker =
                    std::filesystem::absolute(argv[3]).parent_path() / "forge_asset_build";
#endif
                prepare(root, example,
                        !example.graph && !example.weighted && !example.sorted &&
                            !example.scene_collision,
                        worker, !texture_prepared);
                texture_prepared = true;
            }
        } else if (action == "--prepare-opacity") {
            // Author the new Effect against the already selected shared Material.
            // Full staging/relocation above republishes the complete target closure.
            prepare(root, fading, false);
        } else if (action == "--verify-export-gpu-sorted-countercases") {
            check(argc == 5, "Need countercase scratch and matching texture worker");
            const auto copy =
                std::filesystem::absolute(argv[3]) / AssetId::generate().str() / "source";
            std::filesystem::create_directories(copy.parent_path());
            std::filesystem::copy(root, copy, std::filesystem::copy_options::recursive);
            verify_sorted_countercases(copy, std::filesystem::absolute(argv[4]));
        } else if (action == "--verify-export" || action == "--verify-export-opacity" ||
                   action == "--verify-export-billboards" ||
                   action == "--verify-export-gpu-billboards" ||
                   action == "--verify-export-gpu-graph-billboards" ||
                   action == "--verify-export-trails" || action == "--verify-export-collision" ||
                   action == "--verify-export-weighted" ||
                   action == "--verify-export-weighted-graph" ||
                   action == "--verify-export-gpu-opacity" ||
                   action == "--verify-export-gpu-graph-opacity" ||
                   action == "--verify-export-gpu-collision" ||
                   action == "--verify-export-gpu-trails" ||
                   action == "--verify-export-gpu-sorted" ||
                   action == "--verify-export-scene-collision" ||
                   action == "--verify-export-xy-scene-collision" ||
                   action == "--verify-export-scene-impacts" ||
                   action == "--verify-export-xy-scene-impacts" ||
                   action == "--verify-export-resting" || action == "--verify-export-xy-resting" ||
                   action == "--verify-export-world-trails" ||
                   action == "--verify-export-xy-world-trails") {
            const bool gpu = action == "--verify-export-gpu-sorted" ||
                             action == "--verify-export-gpu-opacity" ||
                             action == "--verify-export-gpu-graph-opacity" ||
                             action == "--verify-export-weighted" ||
                             action == "--verify-export-weighted-graph" ||
                             action == "--verify-export-gpu-billboards" ||
                             action == "--verify-export-gpu-graph-billboards";
            const bool billboards = action == "--verify-export-billboards" ||
                                    action == "--verify-export-xy-resting" ||
                                    action == "--verify-export-scene-collision" ||
                                    action == "--verify-export-xy-scene-collision" || gpu;
            check(argc == (billboards ? 5 : 4),
                  "Need private verification scratch root and optional texture worker");
            const auto copy =
                std::filesystem::absolute(argv[3]) / AssetId::generate().str() / "source";
            std::filesystem::create_directories(copy.parent_path());
            std::filesystem::copy(root, copy, std::filesystem::copy_options::recursive);
            verify_export(copy,
                          action == "--verify-export-resting"                ? scene_resting
                          : action == "--verify-export-xy-resting"           ? xy_scene_resting
                          : action == "--verify-export-scene-impacts"        ? scene_impacts
                          : action == "--verify-export-xy-scene-impacts"     ? xy_scene_impacts
                          : action == "--verify-export-world-trails"         ? world_ribbons
                          : action == "--verify-export-xy-world-trails"      ? xy_world_ribbons
                          : action == "--verify-export-xy-scene-collision"   ? xy_scene_colliding
                          : action == "--verify-export-scene-collision"      ? scene_colliding
                          : action == "--verify-export-gpu-sorted"           ? gpu_sorted
                          : action == "--verify-export-gpu-trails"           ? gpu_ribbons
                          : action == "--verify-export-gpu-collision"        ? gpu_colliding
                          : action == "--verify-export-gpu-opacity"          ? gpu_fading
                          : action == "--verify-export-gpu-graph-opacity"    ? graph_fading
                          : action == "--verify-export-weighted"             ? weighted_quads
                          : action == "--verify-export-weighted-graph"       ? weighted_graph_quads
                          : action == "--verify-export-collision"            ? colliding
                          : action == "--verify-export-trails"               ? ribbons
                          : action == "--verify-export-gpu-graph-billboards" ? graph_quads
                          : gpu                                              ? gpu_quads
                          : billboards                                       ? quads
                          : action == "--verify-export"                      ? alpha
                                                                             : fading,
                          billboards ? std::filesystem::absolute(argv[4])
                                     : std::filesystem::path{});
        } else
            throw std::runtime_error("Unknown sorted-alpha Gallery action");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
