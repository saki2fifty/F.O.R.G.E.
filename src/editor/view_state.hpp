#pragma once
#include "document.hpp"
#include "grid.hpp"
namespace forge {
inline std::string view_key(const SceneDocument& document) {
    return document.path().empty()
               ? "untitled"
               : path_text(document.path().lexically_relative(document.project()));
}
inline void validate_editor_view(const EditorCamera& camera, GridPlane plane) {
    for (float value : camera.target)
        if (!std::isfinite(value) || std::abs(value) > 1000000)
            throw std::runtime_error("Invalid bookmark position");
    if (!std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) ||
        !std::isfinite(camera.distance) || std::abs(camera.pitch) > EditorCamera::pole ||
        camera.distance < .25f || camera.distance > 100000 ||
        (camera.projection != CameraProjection::Perspective && !camera.orthographic()) ||
        !std::isfinite(camera.orthographic_height) ||
        camera.orthographic_height < EditorCamera::minimum_height ||
        camera.orthographic_height > EditorCamera::maximum_height ||
        (plane != GridPlane::XZ && plane != GridPlane::XY && plane != GridPlane::YZ))
        throw std::runtime_error("Invalid bookmark camera or grid plane");
}
inline void save_view(const SceneDocument& document, const EditorCamera& camera,
                      GridPlane plane = GridPlane::XZ) {
    document.check_ownership();
    validate_editor_view(camera, plane);
    const auto path = project_control_file(document.project(), "editor-views.json");
    auto data = std::filesystem::exists(path) ? read_json(path)
                                              : Json{{"version", 1}, {"views", Json::object()}};
    if (data.at("version") != 1 || !data.at("views").is_object())
        throw std::runtime_error("Invalid view bookmark file; original preserved");
    auto& entry = data["views"][view_key(document)];
    if (entry.is_null())
        entry = Json::object();
    if (!entry.is_object())
        throw std::runtime_error("Invalid view bookmark entry; original preserved");
    entry["target"] = camera.target;
    entry["yaw"] = camera.yaw;
    entry["pitch"] = camera.pitch;
    entry["distance"] = camera.distance;
    entry["projection"] = camera.orthographic() ? "orthographic" : "perspective";
    entry["orthographic_height"] = camera.orthographic_height;
    entry["grid_plane"] = plane == GridPlane::XY ? "xy" : plane == GridPlane::YZ ? "yz" : "xz";
    atomic_write(path, data.dump(2));
}
inline bool restore_view(const SceneDocument& document, EditorCamera& camera,
                         GridPlane* plane = nullptr) {
    const auto path = project_control_file(document.project(), "editor-views.json");
    if (!std::filesystem::exists(path))
        return false;
    const auto data = read_json(path);
    if (data.at("version") != 1 || !data.at("views").is_object())
        throw std::runtime_error("Invalid view bookmark file");
    const auto key = view_key(document);
    if (!data.at("views").contains(key))
        return false;
    const auto& entry = data.at("views").at(key);
    auto next = camera;
    next.target = entry.at("target").get<EditorCamera::Vec>();
    next.yaw = entry.at("yaw");
    next.pitch = entry.at("pitch");
    next.distance = entry.at("distance");
    // Old version1 entries explicitly restore their original perspective/XZ
    // defaults; they must not inherit a previously opened Scene's projection.
    const auto projection = entry.value("projection", std::string("perspective"));
    if (projection != "perspective" && projection != "orthographic")
        throw std::runtime_error("Invalid bookmark projection");
    next.projection = projection == "orthographic" ? CameraProjection::Orthographic
                                                   : CameraProjection::Perspective;
    next.orthographic_height = entry.value("orthographic_height", 10.f);
    const auto grid = entry.value("grid_plane", std::string("xz"));
    if (grid != "xz" && grid != "xy" && grid != "yz")
        throw std::runtime_error("Invalid bookmark grid plane");
    const auto next_plane = grid == "xy"   ? GridPlane::XY
                            : grid == "yz" ? GridPlane::YZ
                                           : GridPlane::XZ;
    validate_editor_view(next, next_plane);
    camera = next;
    if (plane)
        *plane = next_plane;
    return true;
}
} // namespace forge
