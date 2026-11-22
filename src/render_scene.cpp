#include "builtins.hpp"
#include "camera_aim.hpp"
#include "render_camera_snapshot.hpp"
#include "ui_surface_limits.hpp"
#include <algorithm>
#include <cmath>
#include <forge/content_digest.hpp>
#include <forge/engine_assets.hpp>
#include <forge/particle_effect.hpp>
#include <forge/render_scene.hpp>
#include <limits>
#include <set>
namespace forge {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Diagnostic diagnostic(AssetId scene, std::optional<EntityId> entity, const char* category,
                      const std::string& message, const char* property = "") {
    Diagnostic result{Severity::Error, category, message.substr(0, 4096), {}};
    result.context.asset = scene;
    result.context.entity = entity;
    result.context.property = property;
    result.context.source = "presentation";
    return result;
}
template <class Result> void report(Result& result, Diagnostic d) {
    // Match the existing diagnostics service count; retain first actionable issues
    // and an explicit omitted count rather than building an unbounded error wall.
    if (result.diagnostics.size() < 256)
        result.diagnostics.push_back(std::move(d));
    else
        ++result.omitted_diagnostics;
}
AffineTransform world_transform(const Json& row, const char* field = "world_affine") {
    require(row.value("spatial_resolved", false),
            "Render component requires a resolved world transform");
    const auto& values = row.at(field);
    require(values.is_array() && values.size() == 12,
            "Render component requires a copied 3x4 world matrix");
    AffineTransform result;
    for (unsigned i = 0; i < 12; ++i) {
        require(values[i].is_number(), "World matrix contains a nonnumeric value");
        result.m[i] = values[i].get<double>();
        require(std::isfinite(result.m[i]), "World matrix contains a nonfinite value");
    }
    return result;
}
template <class T> T component(const Json& values, const char* name) {
    const auto& raw = values.at(name);
    detail::validate_components(Json{{name, raw}});
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == name)
            return std::get<T>(type.decode(raw));
    throw std::runtime_error("Render component is not registered");
}
[[noreturn]] void camera_failure(detail::CameraViewModifier kind, EntityId camera,
                                 std::string message) {
    if (kind == detail::CameraViewModifier::Aim)
        throw detail::CameraAimFailure(camera, std::move(message));
    throw detail::CameraFollowFailure(camera, std::move(message));
}
template <class T>
T camera_modifier(const Json& values, const char* name, EntityId camera,
                  detail::CameraViewModifier kind) {
    try {
        return component<T>(values, name);
    } catch (const std::exception& error) {
        camera_failure(kind, camera, error.what());
    }
}
Double3 camera_target(AssetId scene, EntityRef reference, Double3 offset, bool local,
                      const detail::JsonRowIndex& rows, const char* label) {
    const auto require_target = [&](bool value, const char* reason) {
        if (!value)
            throw std::runtime_error(std::string(label) + reason);
    };
    require_target(reference.scene == scene, " target belongs to a different Scene");
    const auto target = rows.find(reference.entity.str());
    require_target(target != rows.end(), " target is unavailable in this Scene");
    require_target(!target->second->value("prefab", false),
                   " target is a Prefab declaration rather than an instance");
    const auto world = world_transform(*target->second);
    if (local)
        return world.point(offset);
    auto point = world.point({0, 0, 0});
    for (unsigned index = 0; index < 3; ++index)
        point[index] += offset[index];
    return point;
}
} // namespace
RenderCamera detail::snapshot_camera_view(AssetId scene, EntityId camera, Camera settings,
                                          const JsonRowIndex& rows, bool use_projected) {
    const auto at = rows.find(camera.str());
    require(at != rows.end(), "Camera view is missing from copied presentation");
    const auto& row = *at->second;
    for (const auto kind : {CameraViewModifier::Follow, CameraViewModifier::Aim}) {
        const auto* key = CameraViewFailure::projection_key(kind);
        if (!use_projected || !row.contains(key))
            continue;
        const auto& error = row.at(key);
        const auto origin = error.at("entity").get<EntityId>();
        const auto& message = error.at("message").get_ref<const std::string&>();
        require(rows.contains(origin.str()) && !message.empty() && message.size() <= 4096,
                "Malformed Camera modifier error projection");
        camera_failure(kind, origin, message);
    }
    CameraAim aim;
    const auto& values = row.at("components");
    const bool has_aim = values.contains("forge.camera_aim");
    if (has_aim)
        aim =
            camera_modifier<CameraAim>(values, "forge.camera_aim", camera, CameraViewModifier::Aim);
    CameraFollow follow;
    const bool has_follow = values.contains("forge.camera_follow");
    if (has_follow)
        follow = camera_modifier<CameraFollow>(values, "forge.camera_follow", camera,
                                               CameraViewModifier::Follow);
    if (use_projected && row.contains("camera_world_affine"))
        return {camera, settings, world_transform(row, "camera_world_affine")};
    const auto world = world_transform(row);
    const bool aim_active = has_aim && aim.enabled && aim.target.entity && aim.weight > 0;
    const bool follow_active =
        has_follow && follow.enabled && follow.target.entity && follow.weight > 0;
    if (!aim_active && !follow_active)
        return {camera, settings, world};
    // Reject invalid source cameras before considering a modifier or target.
    auto rigid = camera_rigid_transform(world);
    if (follow_active) {
        try {
            const auto eye = camera_target(
                scene, follow.target, {follow.offset_x, follow.offset_y, follow.offset_z},
                follow.offset_space == std::uint32_t(CameraFollowSpace::Target), rows,
                "Camera Follow");
            for (unsigned index = 0; index < 3; ++index) {
                require(std::isfinite(eye[index]), "Camera Follow desired eye must be finite");
                rigid.m[4 * index + 3] =
                    std::lerp(rigid.m[4 * index + 3], eye[index], follow.weight);
            }
        } catch (const std::exception& error) {
            throw CameraFollowFailure(camera, error.what());
        }
    }
    if (!aim_active)
        return {camera, settings, rigid};
    try {
        const auto point =
            camera_target(scene, aim.target, {aim.offset_x, aim.offset_y, aim.offset_z}, true, rows,
                          "Camera Aim");
        return {camera, settings, aim_camera_transform(settings, rigid, point, aim.weight)};
    } catch (const std::exception& error) {
        throw CameraAimFailure(camera, error.what());
    }
}
NodePolicies extract_node_policies(const Json& source) {
    NodePolicies result;
    const auto scene = source.at("asset_id").get<AssetId>();
    const auto& entities = source.at("entities");
    require(entities.is_array() && entities.size() <= 10000,
            "Node policy snapshot exceeds the 10000-entity profile");
    struct Entry {
        EntityId parent;
        NodePolicy own;
        unsigned visit = 0;
    };
    std::map<EntityId, Entry> entries;
    std::set<EntityId> identities;
    for (const auto& row : entities) {
        const auto id = row.at("id").get<EntityId>();
        require(identities.insert(id).second, "Node policy snapshot repeats an entity identity");
        auto& entry = entries[id];
        try {
            if (row.contains("parent") && !row.at("parent").is_null())
                entry.parent = row.at("parent").get<EntityId>();
            const auto& values = row.at("components");
            require(values.is_object(), "Node policy components must be an object");
            if (values.contains("forge.node_visibility"))
                entry.own.visible =
                    component<NodeVisibility>(values, "forge.node_visibility").visible;
            if (values.contains("forge.node_selectability"))
                entry.own.selectable =
                    component<NodeSelectability>(values, "forge.node_selectability").selectable;
        } catch (const std::exception& e) {
            entry.own = {false, false, false};
            report(result, diagnostic(scene, id, "render.node_policy.invalid", e.what()));
        }
    }
    // Iterative DFS with memoization: no recursion proportional to an untrusted
    // hierarchy, and shared ancestors are resolved only once.
    std::vector<EntityId> path;
    for (auto& [id, entry] : entries) {
        if (entry.visit == 2)
            continue;
        path.clear();
        EntityId cursor = id;
        NodePolicy ancestor;
        while (cursor) {
            auto next = entries.find(cursor);
            if (next == entries.end() || next->second.visit == 1) {
                ancestor = {false, false, false};
                report(result,
                       diagnostic(scene, id, "render.node_policy.ancestry",
                                  next == entries.end() ? "Structural parent is missing"
                                                        : "Structural parent chain is cyclic"));
                break;
            }
            if (next->second.visit == 2) {
                ancestor = result.entities.at(cursor);
                break;
            }
            next->second.visit = 1;
            path.push_back(cursor);
            cursor = next->second.parent;
        }
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            auto& own = entries.at(*it);
            ancestor = {ancestor.visible && own.own.visible,
                        ancestor.selectable && own.own.selectable, ancestor.valid && own.own.valid};
            result.entities.emplace(*it, ancestor);
            own.visit = 2;
        }
    }
    return result;
}
RenderScene extract_render_scene(const Json& source) {
    require(source.is_object(), "Presentation scene must be an object");
    RenderScene result;
    result.scene = source.at("asset_id").get<AssetId>();
    if (source.contains("simulation_seconds")) {
        const auto& value = source.at("simulation_seconds");
        require(value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= 0,
                "Simulation presentation time must be a finite nonnegative number");
        result.simulation_seconds = value.get<double>();
    }
    try {
        result.settings = scene_render_settings(source);
    } catch (const std::exception& e) {
        report(result,
               diagnostic(result.scene, {}, "render.settings.invalid", e.what(), "rendering"));
    }
    const auto& entities = source.at("entities");
    require(entities.is_array(), "Presentation entities must be an array");
    require(entities.size() <= 10000, "Presentation scene exceeds the 10000-entity profile");
    const auto policy = extract_node_policies(source);
    for (const auto& error : policy.diagnostics)
        report(result, error);
    result.omitted_diagnostics += policy.omitted_diagnostics;
    std::set<EntityId> identities;
    const auto rows = detail::index_json_rows(entities);
    std::map<EntityId, ModelSource> models;
    std::size_t particle_world_count = 0;
    std::size_t particle_world_segments = 0;
    for (const auto& row : entities) {
        require(row.is_object(), "Presentation entity must be an object");
        const auto id = row.at("id").get<EntityId>();
        require(identities.insert(id).second, "Presentation contains duplicate entity identities");
        if (row.value("prefab", false) || !row.value("state_active", true))
            continue;
        const auto state = policy.entities.at(id);
        const auto& values = row.at("components");
        require(values.is_object(), "Presentation components must be an object");
        auto admit = [&](const char* name, const char* category, auto read) {
            if (!values.contains(name))
                return;
            try {
                read();
            } catch (const std::exception& e) {
                report(result, diagnostic(result.scene, id, category, e.what(), name));
            }
        };
        admit("forge.model_source", "render.model.invalid", [&] {
            const auto model = component<ModelSource>(values, "forge.model_source");
            const auto world = world_transform(row);
            models.emplace(id, model);
            result.model_nodes.push_back({id, model, world, {}});
        });
        admit("forge.camera", "render.camera.invalid", [&] {
            const auto c = component<Camera>(values, "forge.camera");
            if (c.enabled)
                try {
                    result.cameras.push_back(
                        detail::snapshot_camera_view(result.scene, id, c, rows));
                } catch (const detail::CameraViewFailure& error) {
                    report(result, diagnostic(result.scene, error.camera, error.category(),
                                              error.what(), error.component()));
                }
        });
        admit("forge.light", "render.light.invalid", [&] {
            const auto l = component<Light>(values, "forge.light");
            if (l.enabled && state.valid && state.visible) {
                const auto world = world_transform(row);
                RenderLight light{id, light_view(l, world), l.texture, {}};
                if (l.texture.id)
                    try {
                        light.image = light_image_frame(l, world);
                    } catch (const std::exception& error) {
                        report(result,
                               diagnostic(result.scene, id, "render.light.image",
                                          std::string(error.what()) + " (ordinary light retained)",
                                          "forge.light.texture"));
                    }
                result.lights.push_back(std::move(light));
            }
        });
        admit("forge.decal", "render.decal.invalid", [&] {
            const auto value = component<DecalProjector>(values, "forge.decal");
            if (value.enabled && value.material.id && value.opacity != 0 && state.valid &&
                state.visible)
                result.decals.push_back({id, decal_view(value, world_transform(row))});
        });
        admit("forge.reflection_probe", "render.probe.invalid", [&] {
            const auto probe = component<ReflectionProbe>(values, "forge.reflection_probe");
            if (probe.enabled && (probe.texture.id || probe.runtime_capture) &&
                (probe.intensity != 0 || probe.diffuse_intensity != 0) && state.valid &&
                state.visible)
                result.reflection_probes.push_back({id, probe, world_transform(row)});
        });
        admit("forge.fog_volume", "render.fog.invalid", [&] {
            const auto volume = component<FogVolume>(values, "forge.fog_volume");
            if (volume.enabled && volume.density > 0 && volume.layers != 0 && state.valid &&
                state.visible)
                result.fog_volumes.push_back({id, volume, world_transform(row)});
        });
        // Build-63 compatibility: explicit Mesh/Tile Map renderers own rendering
        // when present, including disabled/missing assignments. Do not create a cube
        // behind an intentionally invisible or invalid renderer.
        if (!values.contains("forge.mesh_renderer") &&
            !values.contains("forge.tile_map_renderer") &&
            (values.contains("forge.local_translation") || values.contains("forge.position"))) {
            try {
                const auto primitive = values.contains("forge.primitive")
                                           ? component<Primitive>(values, "forge.primitive")
                                           : Primitive{};
                if (primitive.kind != no_primitive) {
                    const auto tint = values.contains("forge.tint")
                                          ? component<Tint>(values, "forge.tint")
                                          : Tint{};
                    MeshRenderer renderer;
                    renderer.visible = state.valid && state.visible;
                    renderer.mesh = engine_primitive(primitive.kind);
                    renderer.materials.push_back(
                        {"surface", engine_material(EngineMaterial::LegacyBlockout)});
                    result.meshes.push_back({id,
                                             renderer,
                                             world_transform(row),
                                             {{tint.r, tint.g, tint.b}},
                                             state.valid && state.selectable});
                }
            } catch (const std::exception& e) {
                report(result, diagnostic(result.scene, id, "render.primitive.invalid", e.what(),
                                          "forge.primitive"));
            }
        }
        const auto mesh_start = result.meshes.size();
        admit("forge.mesh_renderer", "render.mesh.invalid", [&] {
            auto mesh = component<MeshRenderer>(values, "forge.mesh_renderer");
            if (mesh.enabled) {
                mesh.visible = mesh.visible && state.valid && state.visible;
                require(bool(mesh.mesh.id), "Mesh Renderer has no assigned mesh asset");
                result.meshes.push_back(
                    {id, mesh, world_transform(row), {}, state.valid && state.selectable});
            }
        });
        admit("forge.ui_surface_input", "render.ui_surface.invalid", [&] {
            const auto input = component<UiSurfaceInput>(values, "forge.ui_surface_input");
            if (!input.enabled || !input.render_texture.id || !state.valid || !state.visible)
                return;
            require(values.contains("forge.mesh_renderer"),
                    "UI Surface Input requires a Mesh Renderer");
            const auto mesh = component<MeshRenderer>(values, "forge.mesh_renderer");
            if (!mesh.enabled || !mesh.visible)
                return;
            if (input.geometry == std::uint32_t(UiSurfaceGeometry::Quad))
                require(mesh.mesh == engine_primitive(8),
                        "UI Surface Input Quad geometry requires Engine / Quad");
            require(result.meshes.size() > mesh_start && result.meshes.back().entity == id,
                    "UI Surface Input Mesh Renderer is invalid");
            require(result.ui_surfaces.size() < ui_surface_detail::max_surfaces,
                    "Scene exceeds 64 interactive UI surfaces");
            result.ui_surfaces.push_back({id, input, std::uint32_t(result.meshes.size() - 1)});
        });
        admit("forge.sprite_renderer", "render.sprite.invalid", [&] {
            auto sprite = component<SpriteRenderer>(values, "forge.sprite_renderer");
            if (!sprite.enabled)
                return;
            require(bool(sprite.material.id), "Sprite Renderer has no assigned image material");
            sprite.visible = sprite.visible && state.valid && state.visible;
            result.sprites.push_back(
                {id, sprite, world_transform(row), state.valid && state.selectable});
        });
        admit("forge.tile_map_renderer", "render.tile_map.invalid", [&] {
            auto map = component<TileMapRenderer>(values, "forge.tile_map_renderer");
            if (!map.enabled)
                return;
            require(bool(map.map.id), "Tile Map Renderer has no assigned map asset");
            map.visible = map.visible && state.valid && state.visible;
            result.tile_maps.push_back(
                {id, map, world_transform(row), state.valid && state.selectable});
        });
        admit("forge.particle_emitter", "render.particles.invalid", [&] {
            auto emitter = component<ParticleEmitter>(values, "forge.particle_emitter");
            if (!emitter.enabled)
                return;
            require(bool(emitter.effect.id), "Particle Emitter has no assigned Particle Effect");
            require(result.particles.size() < particle_emitter_limit,
                    "Scene exceeds the 64 enabled-emitter budget");
            emitter.visible = emitter.visible && state.valid && state.visible;
            RenderParticleEmitter particle{id, emitter, world_transform(row)};
            if (row.contains("particle_playback")) {
                const auto& playback = row.at("particle_playback");
                require(playback.at("time").is_number() &&
                            playback.at("generation").is_number_unsigned() &&
                            playback.at("seed").is_number_unsigned() &&
                            playback.at("seed").get<std::uint64_t>() <= UINT32_MAX,
                        "Particle playback capture has invalid scalar types");
                particle.time = playback.at("time").get<double>();
                particle.revision = playback.at("revision").get<std::string>();
                particle.generation = playback.at("generation").get<std::uint64_t>();
                require(std::isfinite(particle.time) && particle.time >= 0 &&
                            particle.time <= 1e9 && valid_content_digest(particle.revision) &&
                            particle.generation &&
                            playback.at("seed").get<std::uint32_t>() == emitter.seed,
                        "Particle playback capture does not match its emitter");
                if (playback.contains("world_samples")) {
                    particle.world_samples =
                        decode_particle_world_samples(playback.at("world_samples"));
                }
                if (playback.contains("world_paths")) {
                    particle.world_paths = decode_particle_world_paths(playback.at("world_paths"));
                    require(particle.world_paths->paths.segment_count() <=
                                particle_scene_capacity - particle_world_segments,
                            "Particle World paths exceed the 8192-segment Scene budget");
                }
            }
            if (particle.world_samples || particle.world_paths) {
                const auto births = particle_world_capture_birth_count(
                    particle.world_samples ? std::span(*particle.world_samples)
                                           : std::span<const ParticleWorldSample>{},
                    particle.world_paths ? &*particle.world_paths : nullptr);
                require(births <= particle_scene_collision_capacity - particle_world_count,
                        "Particle World capture exceeds the 1024-birth Scene budget");
                particle_world_count += births;
            }
            if (particle.world_paths)
                particle_world_segments += particle.world_paths->paths.segment_count();
            result.particles.push_back(std::move(particle));
        });
    }
    std::size_t animation_joint_budget = 8192;
    for (auto& node : result.model_nodes) {
        try {
            EntityId cursor = node.entity;
            std::set<EntityId> visited;
            while (cursor && visited.insert(cursor).second) {
                const auto row = rows.find(cursor.str());
                require(row != rows.end() && !row->second->value("prefab", false),
                        "Model instance structural ancestry is missing");
                if (row->second->at("components").contains("forge.model_source")) {
                    const auto ancestor = models.find(cursor);
                    require(ancestor != models.end(), "Model ancestor provenance is invalid");
                    if (!ancestor->second.node.id) {
                        require(ancestor->second.model == node.source.model,
                                "Model node is beneath a different model root");
                        node.root = cursor;
                        break;
                    }
                }
                cursor = row->second->contains("parent") && !row->second->at("parent").is_null()
                             ? row->second->at("parent").get<EntityId>()
                             : EntityId{};
            }
            require(bool(node.root),
                    "Model node is outside its instance root or ancestry is cyclic");
            const auto& row = *rows.at(node.entity.str());
            if (!row.contains("model_animation_ready"))
                continue; // Authoring snapshot or model without an Animator.
            require(node.entity == node.root,
                    "Runtime model animation must belong to its instance root");
            RenderModelAnimation animation;
            animation.root = node.root;
            animation.model = node.source.model.id;
            // Keep an unavailable marker on malformed data too. Rendering may
            // retain a previous complete pose; it must not silently reset weights.
            result.model_animations.push_back(animation);
            require(row.at("model_animation_ready").is_boolean(),
                    "Runtime model animation readiness must be boolean");
            if (!row.at("model_animation_ready").get<bool>())
                continue;
            const auto& pose = row.at("animation_pose");
            require(pose.at("model_asset").get<AssetId>() == animation.model,
                    "Model animation pose belongs to a different asset");
            animation.revision = pose.at("model_revision").get<std::string>();
            require(animation.revision.size() == 64 &&
                        std::all_of(animation.revision.begin(), animation.revision.end(),
                                    [](char c) {
                                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                    }),
                    "Model animation revision is invalid");
            const auto& indices = pose.at("joint_nodes");
            const auto& assets = pose.at("joint_assets");
            require(indices.is_array() && assets.is_array() && !indices.empty() &&
                        indices.size() <= 1024 && indices.size() <= animation_joint_budget &&
                        assets.size() == indices.size(),
                    "Model animation node mapping exceeds profile or differs in size");
            animation_joint_budget -= indices.size();
            std::map<std::size_t, AssetId> mapped;
            std::set<AssetId> unique_assets;
            for (std::size_t i = 0; i < indices.size(); ++i) {
                require(indices[i].is_number_unsigned() ||
                            (indices[i].is_number_integer() && indices[i].get<std::int64_t>() >= 0),
                        "Model animation node index is invalid");
                require(indices[i].get<std::size_t>() < 100000,
                        "Model animation node index exceeds source profile");
                const auto asset = assets[i].get<AssetId>();
                require(bool(asset) &&
                            mapped.emplace(indices[i].get<std::size_t>(), asset).second &&
                            unique_assets.insert(asset).second,
                        "Model animation repeats a node identity");
            }
            const auto& morphs = pose.at("morphs");
            require(morphs.is_array() && morphs.size() <= mapped.size(),
                    "Model morph node count exceeds its rig");
            for (const auto& morph : morphs) {
                const auto index = morph.at("node").get<std::size_t>();
                require(mapped.contains(index), "Model morph target is outside its joint mapping");
                const auto& weights = morph.at("weights");
                require(weights.is_array() && !weights.empty() && weights.size() <= 256,
                        "Model morph weight count exceeds the draw profile");
                std::vector<float> values;
                for (const auto& weight : weights) {
                    require(weight.is_number() && std::isfinite(weight.get<double>()) &&
                                std::abs(weight.get<double>()) <=
                                    std::numeric_limits<float>::max() &&
                                (weight.get<double>() == 0 || weight.get<float>() != 0),
                            "Model morph weight is not finite binary32");
                    values.push_back(weight.get<float>());
                }
                require(animation.morphs.emplace(mapped.at(index), std::move(values)).second,
                        "Model morph node occurs more than once");
            }
            animation.ready = true;
            result.model_animations.back() = std::move(animation);
        } catch (const std::exception& error) {
            report(result, diagnostic(result.scene, node.entity, "render.model.binding",
                                      error.what(), "forge.model_source"));
        }
    }
    std::sort(result.cameras.begin(), result.cameras.end(), [](const auto& a, const auto& b) {
        return a.camera.order != b.camera.order ? a.camera.order < b.camera.order
                                                : a.entity < b.entity;
    });
    std::sort(result.lights.begin(), result.lights.end(),
              [](const auto& a, const auto& b) { return a.entity < b.entity; });
    std::sort(result.meshes.begin(), result.meshes.end(),
              [](const auto& a, const auto& b) { return a.entity < b.entity; });
    // Indices belong to the final detached ordering, not authoring/ECS traversal.
    for (auto& surface : result.ui_surfaces) {
        const auto mesh =
            std::lower_bound(result.meshes.begin(), result.meshes.end(), surface.entity,
                             [](const auto& row, EntityId id) { return row.entity < id; });
        surface.mesh_index = std::uint32_t(mesh - result.meshes.begin());
    }
    return result;
}
CameraComposition prepare_game_cameras(const RenderScene& scene, std::uint32_t width,
                                       std::uint32_t height, AssetRef<RenderTextureAsset> output) {
    CameraComposition result;
    for (const auto& camera : scene.cameras) {
        if (!camera.camera.enabled || camera.camera.render_texture != output)
            continue;
        try {
            result.cameras.push_back({camera.entity, camera.camera,
                                      camera_view(camera.camera, camera.world, width, height)});
        } catch (const std::exception& e) {
            report(result, diagnostic(scene.scene, camera.entity, "render.camera.unavailable",
                                      e.what(), "forge.camera"));
        }
    }
    std::sort(result.cameras.begin(), result.cameras.end(), [](const auto& a, const auto& b) {
        return a.settings.order != b.settings.order ? a.settings.order < b.settings.order
                                                    : a.entity < b.entity;
    });
    if (result.cameras.empty())
        report(result, diagnostic(scene.scene, {}, "render.camera.missing",
                                  output.id ? "No enabled valid render texture camera is available"
                                            : "No enabled valid game camera is available"));
    return result;
}
} // namespace forge
