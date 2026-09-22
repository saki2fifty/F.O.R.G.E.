#pragma once
#include "gltf_validation.hpp"
#include <charconv>
#include <optional>

namespace forge::asset_detail {
struct GltfAnimationTarget {
    std::size_t node{};
    std::string path;
    bool pointer = false;
};
// One semantic resolver for animation validation, node evidence, LOD restrictions
// and instance expansion. Source JSON stays unchanged. This is deliberately the
// existing whole-node TRS/morph animation domain, not a generic property evaluator.
inline std::optional<GltfAnimationTarget> gltf_animation_target(const gltf_detail::Json& target,
                                                                std::size_t nodes) {
    using namespace gltf_detail;
    if (!target.is_object())
        throw std::runtime_error("glTF animation target must be an object");
    const auto path = target.at("path").get<std::string>();
    const auto* ext = extension(target, "KHR_animation_pointer");
    GltfAnimationTarget result;
    if (path == "pointer" || ext) {
        if (path != "pointer" || !ext || target.contains("node"))
            throw std::runtime_error(
                "KHR_animation_pointer requires path=pointer, its payload, and no target.node");
        const auto& pointer = ext->at("pointer").get_ref<const std::string&>();
        constexpr std::string_view prefix = "/nodes/";
        if (pointer.size() > 4096 || !pointer.starts_with(prefix))
            throw std::runtime_error(
                "KHR_animation_pointer: only whole node TRS/morph targets are supported");
        const std::string_view suffix(pointer.data() + prefix.size(),
                                      pointer.size() - prefix.size());
        const auto slash = suffix.find('/');
        if (slash == std::string_view::npos)
            throw std::runtime_error("KHR_animation_pointer: missing node property");
        const auto index = suffix.substr(0, slash);
        if (index.empty() || (index.size() > 1 && index.front() == '0') || index.front() < '0' ||
            index.front() > '9')
            throw std::runtime_error("KHR_animation_pointer: invalid canonical node index");
        const auto [end, error] =
            std::from_chars(index.data(), index.data() + index.size(), result.node);
        if (error != std::errc{} || end != index.data() + index.size())
            throw std::runtime_error("KHR_animation_pointer: invalid or overflowing node index");
        result.path = suffix.substr(slash + 1);
        result.pointer = true;
    } else {
        // Core glTF explicitly allows channels with no bound node.
        if (!target.contains("node"))
            return {};
        result.node = size_value(target.at("node"));
        result.path = path;
    }
    if (result.node >= nodes)
        throw std::runtime_error("glTF animation target node index is invalid");
    if (result.path != "translation" && result.path != "rotation" && result.path != "scale" &&
        result.path != "weights")
        throw std::runtime_error("glTF animation target property is unsupported: " +
                                 result.path.substr(0, 256));
    return result;
}
} // namespace forge::asset_detail
