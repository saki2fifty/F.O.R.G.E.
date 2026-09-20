#pragma once
#include <forge/render_view.hpp>
#include <forge/services.hpp>
namespace forge {
// Detached presentation values, never a second world or authored hierarchy.
// Input is the existing bounded effective_scene transport used by Editor Play.
struct RenderCamera {
    EntityId entity;
    Camera camera;
    AffineTransform world;
};
struct RenderLight {
    EntityId entity;
    LightView light;
};
struct RenderMesh {
    EntityId entity;
    MeshRenderer renderer;
    AffineTransform world;
};
struct RenderScene {
    AssetId scene;
    std::vector<RenderCamera> cameras;
    std::vector<RenderLight> lights;
    std::vector<RenderMesh> meshes;
    std::vector<Diagnostic> diagnostics;
    std::size_t omitted_diagnostics = 0;
};
// Malformed envelope/identity rejects the whole snapshot. Invalid per-component
// producer values are diagnosed and omitted without touching authored state.
RenderScene extract_render_scene(const nlohmann::json& effective_scene);
struct PreparedCamera {
    EntityId entity;
    Camera settings;
    CameraView view;
};
struct CameraComposition {
    std::vector<PreparedCamera> cameras;
    std::vector<Diagnostic> diagnostics;
    std::size_t omitted_diagnostics = 0;
};
// All enabled cameras in order, with identity tie-break. An empty valid selection
// emits a diagnostic; the editor navigation camera is never substituted here.
CameraComposition prepare_game_cameras(const RenderScene&, std::uint32_t width,
                                       std::uint32_t height);
} // namespace forge
