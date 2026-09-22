#include "gltf_instances.hpp"
#include "gltf_animation_target.hpp"
#include "gltf_transform.hpp"
#include "gltf_validation.hpp"
#include <algorithm>
#include <cmath>
namespace forge::asset_detail {
namespace {
using namespace gltf_detail;
constexpr const char* name = "EXT_mesh_gpu_instancing";
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(std::string(name) + ": " + message);
}
} // namespace
std::unique_ptr<GltfSourceBundle> expand_gltf_instances(const NativeGltfDocument& native) {
    const auto& captured = native.source();
    const auto& source_nodes = array(captured.document, "nodes", 100000);
    std::unique_ptr<GltfSourceBundle> result;
    std::map<std::size_t, std::vector<std::size_t>> expanded;
    std::size_t decoded = 0;
    for (std::size_t node_index = 0; node_index < source_nodes.size(); ++node_index) {
        const auto& original = source_nodes[node_index];
        const auto* ext = extension(original, name);
        if (!ext)
            continue;
        const auto& used = array(captured.document, "extensionsUsed", 256);
        require(std::find(used.begin(), used.end(), name) != used.end(), "extension is undeclared");
        require(original.contains("mesh"), "instancing requires a mesh node");
        require(
            !original.contains("skin"),
            "instanced skin binding is not yet representable by the current model draw contract");
        require(ext->contains("attributes") && ext->at("attributes").is_object() &&
                    !ext->at("attributes").empty(),
                "attributes must be a nonempty object");
        const auto& attributes = ext->at("attributes");
        require(attributes.size() <= 64, "attribute count exceeds admission profile");
        const auto& accessors = array(captured.document, "accessors", 100000);
        std::map<std::string, NativeGltfValues<float>> values;
        std::size_t count = 0;
        bool custom = false;
        for (const auto& [semantic, index_json] : attributes.items()) {
            const auto index = size_value(index_json);
            require(index < accessors.size(), "invalid attribute accessor");
            const auto& accessor = accessors[index];
            const auto n = size_value(accessor.at("count"));
            require(n && n <= 100000 && (!count || count == n),
                    "attribute counts differ or exceed limits");
            count = n;
            const bool rotation = semantic == "ROTATION";
            const bool vector = semantic == "TRANSLATION" || semantic == "SCALE";
            if (!rotation && !vector) {
                require(!semantic.empty() && semantic.front() == '_',
                        "unknown standard attribute semantic");
                custom = true;
                continue; // Captured bytes remain the authority for application-specific data.
            }
            const auto component = size_value(accessor.at("componentType"));
            const bool normalized = accessor.value("normalized", false);
            require(accessor.at("type") == (rotation ? "VEC4" : "VEC3") &&
                        ((component == 5126 && !normalized) ||
                         (rotation && normalized && (component == 5120 || component == 5122))),
                    "invalid transform accessor shape/component/normalization");
            const auto width = rotation ? 4u : 3u;
            require(n <= (64 * 1024 * 1024 - decoded) / (width * sizeof(float)),
                    "decoded transforms exceed 64 MiB");
            decoded += n * width * sizeof(float);
            auto loaded = native.floats(index);
            require(loaded.count == n && loaded.components == width &&
                        std::all_of(loaded.values.begin(), loaded.values.end(),
                                    [](float v) { return std::isfinite(v); }),
                    "decoded transform is invalid or nonfinite");
            if (rotation && component != 5126) {
                // Integer normalization quantizes each quaternion lane. Admit
                // unit-length error bounded by that quantization, then normalize
                // the derived FLOAT node value before ordinary TRS validation.
                const double quantum = component == 5120 ? 1. / 127 : 1. / 32767;
                for (std::size_t i = 0; i < n; ++i) {
                    auto* q = loaded.values.data() + 4 * i;
                    const double length =
                        std::hypot(std::hypot(double(q[0]), q[1]), std::hypot(double(q[2]), q[3]));
                    require(length > 0 &&
                                std::abs(length - 1) <=
                                    quantum + 8 * double(std::numeric_limits<float>::epsilon()),
                            "quantized rotation is not a unit quaternion");
                    for (unsigned c = 0; c < 4; ++c)
                        q[c] = float(q[c] / length);
                }
            }
            values.emplace(semantic, std::move(loaded));
        }
        if (!result)
            result = std::make_unique<GltfSourceBundle>(captured);
        auto& nodes = result->document["nodes"];
        require(count <= 100000 - nodes.size(),
                "expanded nodes exceed hierarchy admission profile");
        auto children = original.value("children", Json::array());
        auto& instances = expanded[node_index];
        for (std::size_t i = 0; i < count; ++i) {
            Json node{{"mesh", original.at("mesh")},
                      {"name", original.value("name", std::string("Mesh")) + " instance " +
                                   std::to_string(i + 1)}};
            if (original.contains("weights"))
                node["weights"] = original.at("weights");
            for (const auto& [semantic, property] :
                 {std::pair{"TRANSLATION", "translation"}, std::pair{"ROTATION", "rotation"},
                  std::pair{"SCALE", "scale"}}) {
                if (const auto found = values.find(semantic); found != values.end()) {
                    auto array = Json::array();
                    for (unsigned c = 0; c < found->second.components; ++c)
                        array.push_back(found->second.values[i * found->second.components + c]);
                    node[property] = std::move(array);
                }
            }
            // Exact source TRS validation keeps signed/zero scale; no decomposition.
            (void)gltf_node_matrix(node);
            instances.push_back(nodes.size());
            children.push_back(nodes.size());
            nodes.push_back(std::move(node));
        }
        auto& parent = nodes[node_index];
        parent["children"] = std::move(children);
        parent.erase("mesh");
        parent.erase("weights");
        parent["extensions"].erase(name);
        if (parent["extensions"].empty())
            parent.erase("extensions");
        if (custom)
            result->diagnostics.push_back(std::string(name) + " node " +
                                          std::to_string(node_index) +
                                          ": custom instance attributes retained in captured "
                                          "source; no built-in shader meaning is assigned");
    }
    if (!result)
        return {};
    // TRS animation stays on the original parent. Weight animation addresses
    // each actual mesh copy with the same sampler, without duplicating samples.
    if (result->document.contains("animations"))
        for (auto& animation : result->document["animations"]) {
            Json channels = Json::array();
            for (const auto& channel : array(animation, "channels", 100000)) {
                const auto target =
                    gltf_animation_target(channel.at("target"), source_nodes.size());
                const auto found = target ? expanded.find(target->node) : expanded.end();
                if (target && target->path == "weights" && found != expanded.end()) {
                    require(found->second.size() <= 100000 - channels.size(),
                            "expanded animation channels exceed profile");
                    for (auto node : found->second) {
                        auto copy = channel;
                        if (target->pointer)
                            copy["target"]["extensions"]["KHR_animation_pointer"]["pointer"] =
                                "/nodes/" + std::to_string(node) + "/weights";
                        else
                            copy["target"]["node"] = node;
                        channels.push_back(std::move(copy));
                    }
                } else {
                    require(channels.size() < 100000, "expanded animation channels exceed profile");
                    channels.push_back(channel);
                }
            }
            animation["channels"] = std::move(channels);
        }
    return result;
}
} // namespace forge::asset_detail
