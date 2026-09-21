#include "builtins.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
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
AffineTransform world_transform(const Json& row) {
    require(row.value("spatial_resolved", false),
            "Render component requires a resolved world transform");
    const auto& values = row.at("world_affine");
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
} // namespace
RenderScene extract_render_scene(const Json& source) {
    require(source.is_object(), "Presentation scene must be an object");
    RenderScene result;
    result.scene = source.at("asset_id").get<AssetId>();
    try {
        result.settings = scene_render_settings(source);
    } catch (const std::exception& e) {
        report(result,
               diagnostic(result.scene, {}, "render.settings.invalid", e.what(), "rendering"));
    }
    const auto& entities = source.at("entities");
    require(entities.is_array() && entities.size() <= 10000,
            "Presentation scene exceeds the entity profile");
    std::set<EntityId> identities;
    for (const auto& row : entities) {
        require(row.is_object(), "Presentation entity must be an object");
        const auto id = row.at("id").get<EntityId>();
        require(identities.insert(id).second, "Presentation contains duplicate entity identities");
        if (row.value("prefab", false))
            continue;
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
        admit("forge.camera", "render.camera.invalid", [&] {
            const auto c = component<Camera>(values, "forge.camera");
            if (c.enabled)
                result.cameras.push_back({id, c, world_transform(row)});
        });
        admit("forge.light", "render.light.invalid", [&] {
            const auto l = component<Light>(values, "forge.light");
            if (l.enabled)
                result.lights.push_back({id, light_view(l, world_transform(row))});
        });
        // Build-63 compatibility: an explicit MeshRenderer owns rendering when
        // present, including disabled/missing assignments. Do not create a cube
        // behind an intentionally invisible or invalid renderer.
        if (!values.contains("forge.mesh_renderer") &&
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
                    renderer.mesh = engine_primitive(primitive.kind);
                    renderer.materials.push_back(
                        {"surface", engine_material(EngineMaterial::LegacyBlockout)});
                    result.meshes.push_back(
                        {id, renderer, world_transform(row), {{tint.r, tint.g, tint.b}}});
                }
            } catch (const std::exception& e) {
                report(result, diagnostic(result.scene, id, "render.primitive.invalid", e.what(),
                                          "forge.primitive"));
            }
        }
        admit("forge.mesh_renderer", "render.mesh.invalid", [&] {
            const auto mesh = component<MeshRenderer>(values, "forge.mesh_renderer");
            if (mesh.enabled && mesh.visible) {
                require(bool(mesh.mesh.id), "Mesh Renderer has no assigned mesh asset");
                result.meshes.push_back({id, mesh, world_transform(row)});
            }
        });
    }
    std::sort(result.cameras.begin(), result.cameras.end(), [](const auto& a, const auto& b) {
        return a.camera.order != b.camera.order ? a.camera.order < b.camera.order
                                                : a.entity < b.entity;
    });
    std::sort(result.lights.begin(), result.lights.end(),
              [](const auto& a, const auto& b) { return a.entity < b.entity; });
    std::sort(result.meshes.begin(), result.meshes.end(),
              [](const auto& a, const auto& b) { return a.entity < b.entity; });
    return result;
}
CameraComposition prepare_game_cameras(const RenderScene& scene, std::uint32_t width,
                                       std::uint32_t height) {
    CameraComposition result;
    for (const auto& camera : scene.cameras) {
        try {
            result.cameras.push_back({camera.entity, camera.camera,
                                      camera_view(camera.camera, camera.world, width, height)});
        } catch (const std::exception& e) {
            report(result, diagnostic(scene.scene, camera.entity, "render.camera.unavailable",
                                      e.what(), "forge.camera"));
        }
    }
    if (result.cameras.empty())
        report(result, diagnostic(scene.scene, {}, "render.camera.missing",
                                  "No enabled valid game camera is available"));
    return result;
}
} // namespace forge
