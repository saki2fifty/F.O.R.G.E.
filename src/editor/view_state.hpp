#pragma once
#include "camera.hpp"
#include "document.hpp"
namespace forge {
inline std::string view_key(const SceneDocument& document) {
    return document.path().empty()
               ? "untitled"
               : path_text(document.path().lexically_relative(document.project()));
}
inline void save_view(const SceneDocument& document, const EditorCamera& camera) {
    document.check_ownership();
    const auto path = project_control_file(document.project(), "editor-views.json");
    auto data = std::filesystem::exists(path) ? read_json(path)
                                              : Json{{"version", 1}, {"views", Json::object()}};
    if (data.at("version") != 1 || !data.at("views").is_object())
        throw std::runtime_error("Invalid view bookmark file; original preserved");
    data["views"][view_key(document)] = {{"target", camera.target},
                                         {"yaw", camera.yaw},
                                         {"pitch", camera.pitch},
                                         {"distance", camera.distance}};
    atomic_write(path, data.dump(2));
}
inline bool restore_view(const SceneDocument& document, EditorCamera& camera) {
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
    for (float value : next.target)
        if (!std::isfinite(value) || std::abs(value) > 1000000)
            throw std::runtime_error("Invalid bookmark position");
    if (!std::isfinite(next.yaw) || !std::isfinite(next.pitch) || !std::isfinite(next.distance) ||
        std::abs(next.pitch) > EditorCamera::pole || next.distance < 0.25f ||
        next.distance > 100000)
        throw std::runtime_error("Invalid bookmark camera");
    camera = next;
    return true;
}
} // namespace forge
