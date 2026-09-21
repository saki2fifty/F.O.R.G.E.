#include "gltf_ozz_transport.hpp"
#include "animation_archive.hpp"
#include "asset_bytes.hpp"
#include "gltf_transform.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <forge/transform.hpp>
#include <set>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t byte_limit = 16 * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
ArtifactFile json_file(std::string name, const Json& doc) {
    const auto text = doc.dump();
    require(text.size() <= byte_limit, "Animation conversion description exceeds limit");
    const auto bytes = std::as_bytes(std::span(text));
    return {std::move(name), {bytes.begin(), bytes.end()}};
}
std::string node_name(std::size_t index) { return "forge_joint_" + std::to_string(index); }
} // namespace
GltfOzzTransport prepare_gltf_ozz_transport(const NativeGltfDocument& native,
                                            const GltfOzzOptions& options, std::stop_token stop) {
    const auto cancelled = [&] {
        require(!stop.stop_requested(), "Animation transport cancelled");
    };
    cancelled();
    require(options.sampling_rate >= 1 && options.sampling_rate <= 240 &&
                std::isfinite(options.constant_duration) && options.constant_duration >= .0001f &&
                options.constant_duration <= 3600,
            "Invalid model animation conversion options");
    const auto& source = native.source().document;
    const auto& hierarchy = native.hierarchy();
    const auto animations = source.value("animations", Json::array());
    require(animations.size() <= 64, "Model animation clip count exceeds 64");
    // Bound aggregate decoded accessor data before asking the native adapter
    // to allocate it. Shared accessors are counted once per clip, matching its
    // cache lifetime. Also bound repeated channel expansion into transport JSON.
    std::size_t decoded_budget = 64 * 1024 * 1024;
    for (const auto& animation : animations) {
        std::set<std::size_t> accessors;
        for (const auto& sampler : animation.at("samplers"))
            for (const auto* key : {"input", "output"})
                accessors.insert(sampler.at(key).get<std::size_t>());
        for (const auto index : accessors) {
            const auto& a = source.at("accessors").at(index);
            const auto type = a.at("type").get<std::string>();
            const unsigned width = type == "SCALAR" ? 1
                                   : type == "VEC3" ? 3
                                   : type == "VEC4" ? 4
                                                    : 16;
            const auto count = a.at("count").get<std::size_t>();
            require(count <= decoded_budget / sizeof(float) / width,
                    "Model animation decoded data exceeds 64 MiB");
            decoded_budget -= count * sizeof(float) * width;
        }
    }
    std::vector<NativeAnimationClip> clips;
    std::set<std::size_t> needed;
    auto include = [&](std::size_t node) {
        while (node != gltf_no_index && needed.insert(node).second) {
            require(needed.size() <= animation_detail::max_joints,
                    "Animated model needs more than 1024 joints and required ancestors");
            node = hierarchy.nodes.at(node).parent;
        }
    };
    for (const auto& skin : hierarchy.skins)
        for (const auto joint : skin.joints)
            include(joint);
    for (std::size_t i = 0; i < animations.size(); ++i) {
        cancelled();
        clips.push_back(native.animation(i));
        require(clips.back().duration <= 3600, "Model clip duration exceeds Ozz profile");
        for (const auto& track : clips.back().tracks)
            include(track.node);
    }
    require(!needed.empty(), "Model has no supported skeletal or morph animation nodes");
    GltfOzzTransport result;
    result.nodes.assign(needed.begin(), needed.end());
    std::map<std::size_t, std::size_t> to_index;
    for (std::size_t i = 0; i < result.nodes.size(); ++i)
        to_index.emplace(result.nodes[i], i);
    Json nodes = Json::array(), roots = Json::array();
    for (const auto original : result.nodes) {
        auto node = canonical_ozz_rest(source.at("nodes").at(original));
        node["name"] = node_name(original);
        auto children = Json::array();
        for (const auto& child : source.at("nodes").at(original).value("children", Json::array())) {
            const auto c = child.get<std::size_t>();
            if (needed.contains(c))
                children.push_back(to_index.at(c));
        }
        if (!children.empty())
            node["children"] = std::move(children);
        nodes.push_back(std::move(node));
        if (hierarchy.nodes[original].parent == gltf_no_index)
            roots.push_back(to_index.at(original));
    }
    std::vector<std::size_t> pending;
    for (auto i = roots.rbegin(); i != roots.rend(); ++i)
        pending.push_back(i->get<std::size_t>());
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        result.joint_nodes.push_back(result.nodes[node]);
        const auto children = nodes[node].value("children", Json::array());
        for (auto i = children.rbegin(); i != children.rend(); ++i)
            pending.push_back(i->get<std::size_t>());
    }
    require(result.joint_nodes.size() == result.nodes.size(),
            "Animation transport hierarchy lost nodes");
    Json converted{{"asset", {{"version", "2.0"}}},
                   {"nodes", nodes},
                   {"scenes", Json::array({{{"nodes", roots}}})},
                   {"scene", 0},
                   {"bufferViews", Json::array()},
                   {"accessors", Json::array()},
                   {"animations", Json::array()}};
    std::vector<std::byte> data;
    auto accessor = [&](std::span<const float> values, unsigned width, bool time) {
        require(width && values.size() % width == 0 &&
                    values.size() <= (byte_limit - data.size()) / sizeof(float),
                "Animation transport binary budget exceeded");
        const auto offset = data.size();
        for (float value : values) {
            require(std::isfinite(value), "Animation transport contains a nonfinite value");
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                data.push_back(std::byte((bits >> (i * 8)) & 255));
        }
        converted["bufferViews"].push_back(
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", data.size() - offset}});
        Json a{{"bufferView", converted["bufferViews"].size() - 1},
               {"componentType", 5126},
               {"count", values.size() / width},
               {"type", width == 1   ? "SCALAR"
                        : width == 3 ? "VEC3"
                                     : "VEC4"}};
        if (time) {
            a["min"] = {values.front()};
            a["max"] = {values.back()};
        }
        converted["accessors"].push_back(std::move(a));
        return converted["accessors"].size() - 1;
    };
    std::size_t morph_numbers_left = byte_limit / 32;
    // Source semantic evidence does not use candidate array addresses or display
    // labels as durable identity. Duplicate evidence stays ambiguous at publication.
    std::map<std::size_t, std::string> node_context;
    for (const auto original : result.joint_nodes) {
        const auto& node = hierarchy.nodes[original];
        node_context[original] = asset_build_digest(
            {{"name", source.at("nodes").at(original).value("name", std::string{})},
             {"local", node.matrix},
             {"parent",
              node.parent == gltf_no_index ? std::string{} : node_context.at(node.parent)}});
    }
    std::map<const std::vector<float>*, std::string> curve_values;
    const auto curve_digest = [&](const std::shared_ptr<const std::vector<float>>& values) {
        const auto [entry, added] = curve_values.emplace(values.get(), std::string{});
        if (added)
            entry->second = content_digest(std::as_bytes(std::span(*values)));
        return entry->second;
    };
    const auto digest_sorted = [](std::vector<std::string> values) {
        std::sort(values.begin(), values.end());
        return asset_build_digest(values);
    };
    Json config{{"skeleton", {{"filename", "skeleton.ozz"}}}, {"animations", Json::array()}};
    auto clip_metadata = Json::array();
    for (std::size_t c = 0; c < clips.size(); ++c) {
        cancelled();
        const auto& clip = clips[c];
        const auto name = "forge_clip_" + std::to_string(c);
        const auto duration = clip.duration > 0 ? clip.duration : options.constant_duration;
        Json animation{{"name", name}, {"samplers", Json::array()}, {"channels", Json::array()}};
        Json morphs = Json::array(), transform_channels = Json::array();
        std::vector<std::string> content_evidence, semantic_evidence;
        std::set<std::pair<std::size_t, NativeAnimationPath>> bound;
        double trs_duration = 0;
        auto add = [&](std::size_t node, NativeAnimationPath path,
                       NativeAnimationInterpolation interpolation, std::span<const float> times,
                       std::span<const float> values) {
            const auto width = path == NativeAnimationPath::Rotation ? 4u : 3u;
            const auto in = accessor(times, 1, true), out = accessor(values, width, false);
            const auto sampler = animation["samplers"].size();
            animation["samplers"].push_back(
                {{"input", in},
                 {"output", out},
                 {"interpolation", interpolation == NativeAnimationInterpolation::Step ? "STEP"
                                   : interpolation == NativeAnimationInterpolation::Linear
                                       ? "LINEAR"
                                       : "CUBICSPLINE"}});
            animation["channels"].push_back(
                {{"sampler", sampler},
                 {"target",
                  {{"node", to_index.at(node)},
                   {"path", path == NativeAnimationPath::Translation ? "translation"
                            : path == NativeAnimationPath::Rotation  ? "rotation"
                                                                     : "scale"}}}});
        };
        for (const auto& track : clip.tracks) {
            content_evidence.push_back(
                asset_build_digest({{"path", static_cast<unsigned>(track.path)},
                                    {"components", track.components},
                                    {"interpolation", static_cast<unsigned>(track.interpolation)},
                                    {"times", curve_digest(track.times)},
                                    {"values", curve_digest(track.values)}}));
            semantic_evidence.push_back(
                asset_build_digest({{"node", node_context.at(track.node)},
                                    {"path", static_cast<unsigned>(track.path)}}));
            if (track.path == NativeAnimationPath::Weights) {
                // Reserve a conservative serialized-number budget before JSON expansion.
                require(track.times->size() <= morph_numbers_left,
                        "Morph animation metadata exceeds budget");
                morph_numbers_left -= track.times->size();
                require(track.values->size() <= morph_numbers_left,
                        "Morph animation metadata exceeds budget");
                morph_numbers_left -= track.values->size();
                // Preserve exact morph curves separately; gltf2ozz ignores weights.
                morphs.push_back({{"node", track.node},
                                  {"components", track.components},
                                  {"interpolation", static_cast<unsigned>(track.interpolation)},
                                  {"times", *track.times},
                                  {"values", *track.values}});
                continue;
            }
            require(bound.emplace(track.node, track.path).second,
                    "Duplicate animated transform channel");
            transform_channels.push_back(
                {{"node", track.node},
                 {"path", track.path == NativeAnimationPath::Translation ? "translation"
                          : track.path == NativeAnimationPath::Rotation  ? "rotation"
                                                                         : "scale"}});
            trs_duration = std::max(trs_duration, double(track.times->back()));
            // Extend one existing channel with its clamped final value when
            // morph channels are longer. Cubic keeps the preceding segment's
            // incoming derivative and makes only the new tail constant.
            if (trs_duration < duration && animation["channels"].empty()) {
                auto times = *track.times;
                auto values = *track.values;
                const auto width = track.components;
                if (track.interpolation == NativeAnimationInterpolation::CubicSpline) {
                    std::vector<float> last(values.end() - 2 * width, values.end() - width);
                    std::fill(values.end() - width, values.end(), 0.f);
                    values.insert(values.end(), width, 0.f);
                    values.insert(values.end(), last.begin(), last.end());
                    values.insert(values.end(), width, 0.f);
                } else {
                    std::vector<float> last(values.end() - width, values.end());
                    values.insert(values.end(), last.begin(), last.end());
                }
                times.push_back(static_cast<float>(duration));
                add(track.node, track.path, track.interpolation, times, values);
                trs_duration = duration;
            } else
                add(track.node, track.path, track.interpolation, *track.times, *track.values);
        }
        // Preserve source duration when only morph channels reach its end, or a
        // constant clip has a single key at zero. Add an unbound rest channel.
        if (trs_duration < duration) {
            bool added = false;
            for (const auto node : result.nodes) {
                for (const auto path :
                     {NativeAnimationPath::Translation, NativeAnimationPath::Rotation,
                      NativeAnimationPath::Scale}) {
                    if (bound.contains({node, path}))
                        continue;
                    const auto key = path == NativeAnimationPath::Translation ? "translation"
                                     : path == NativeAnimationPath::Rotation  ? "rotation"
                                                                              : "scale";
                    const auto rest = nodes.at(to_index.at(node)).at(key).get<std::vector<float>>();
                    auto values = rest;
                    values.insert(values.end(), rest.begin(), rest.end());
                    const std::array<float, 2> times{0, static_cast<float>(duration)};
                    add(node, path, NativeAnimationInterpolation::Linear, times, values);
                    added = true;
                    break;
                }
                if (added)
                    break;
            }
            require(
                added,
                "Clip duration needs a constant-channel extension; all rig channels are animated");
        }
        converted["animations"].push_back(std::move(animation));
        const auto filename = "clip-" + std::to_string(c) + ".ozz";
        config["animations"].push_back({{"clip", name},
                                        {"filename", filename},
                                        {"iframe_interval", 0},
                                        {"raw", false},
                                        {"additive", false},
                                        {"optimize", options.optimize},
                                        {"sampling_rate", options.sampling_rate}});
        clip_metadata.push_back({{"source_index", c},
                                 {"file", filename},
                                 {"name", animations[c].value("name", std::string{})},
                                 {"duration", duration},
                                 {"morph_tracks", std::move(morphs)},
                                 {"transform_channels", std::move(transform_channels)},
                                 {"content_evidence", digest_sorted(std::move(content_evidence))},
                                 {"semantic_evidence", digest_sorted(std::move(semantic_evidence))},
                                 {"diagnostics", clip.diagnostics}});
    }
    if (!data.empty())
        converted["buffers"] =
            Json::array({{{"byteLength", data.size()}, {"uri", "animation.bin"}}});
    result.converter_inputs.push_back(json_file("source.gltf", converted));
    result.converter_inputs.push_back(json_file("config.json", config));
    if (!data.empty())
        result.converter_inputs.push_back({"animation.bin", std::move(data)});
    Json parents = Json::array(), models = Json::array();
    std::map<std::size_t, std::size_t> joint_index;
    std::vector<AffineTransform> rest_models;
    for (const auto original : result.joint_nodes) {
        const auto& node = hierarchy.nodes[original];
        AffineTransform world;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 4; ++c)
                world.m[r * 4 + c] = node.matrix[c * 4 + r];
        if (node.parent == gltf_no_index)
            parents.push_back(-1);
        else {
            const auto parent = joint_index.at(node.parent);
            parents.push_back(parent);
            world = rest_models.at(parent) * world;
        }
        std::array<double, 16> matrix{};
        matrix[15] = 1;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 4; ++c) {
                const auto x = world.m[r * 4 + c];
                require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max(),
                        "Rig rest model transform exceeds finite float profile");
                matrix[c * 4 + r] = x;
            }
        models.push_back(matrix);
        joint_index.emplace(original, rest_models.size());
        rest_models.push_back(world);
    }
    Json skins = Json::array(), node_skins = Json::array();
    std::size_t skin_joint_budget = 32768;
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::array<float, 16>>> inverse_binds;
    for (std::size_t i = 0; i < hierarchy.skins.size(); ++i) {
        cancelled();
        const auto& skin = hierarchy.skins[i];
        require(skin.joints.size() <= skin_joint_budget,
                "Model skin binding metadata exceeds budget");
        skin_joint_budget -= skin.joints.size();
        std::vector<std::size_t> joints;
        for (const auto node : skin.joints)
            joints.push_back(joint_index.at(node));
        const auto [bind, added] =
            inverse_binds.emplace(std::pair{skin.inverse_bind_accessor, skin.joints.size()},
                                  std::vector<std::array<float, 16>>{});
        if (added)
            bind->second = native.inverse_bind_matrices(i);
        skins.push_back({{"joints", joints}, {"inverse_bind_matrices", bind->second}});
    }
    for (const auto& node : hierarchy.nodes)
        node_skins.push_back(node.skin == gltf_no_index ? Json(nullptr) : Json(node.skin));
    std::map<std::size_t, std::string> subtree;
    std::vector<std::string> rig_roots;
    for (auto it = result.joint_nodes.rbegin(); it != result.joint_nodes.rend(); ++it) {
        const auto original = *it;
        std::vector<std::string> children;
        for (const auto& child : source.at("nodes").at(original).value("children", Json::array())) {
            const auto node = child.get<std::size_t>();
            if (needed.contains(node))
                children.push_back(subtree.at(node));
        }
        subtree[original] = asset_build_digest({{"local", hierarchy.nodes[original].matrix},
                                                {"children", digest_sorted(std::move(children))}});
        if (hierarchy.nodes[original].parent == gltf_no_index)
            rig_roots.push_back(subtree.at(original));
    }
    result.metadata = {{"version", 2},
                       {"nodes", result.nodes},
                       {"joint_nodes", result.joint_nodes},
                       {"joint_parents", parents},
                       {"joint_rest_models", models},
                       {"skins", skins},
                       {"node_skins", node_skins},
                       {"content_evidence", digest_sorted(std::move(rig_roots))},
                       {"semantic_evidence", asset_build_digest({{"role", "combined-model-rig"}})},
                       {"rest_nodes", nodes},
                       {"clips", std::move(clip_metadata)},
                       {"config", std::move(config)}};
    require(result.metadata.dump().size() <= byte_limit,
            "Animation conversion metadata exceeds budget");
    cancelled();
    return result;
}
} // namespace forge::asset_detail
