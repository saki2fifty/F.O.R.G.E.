#pragma once
#include <array>
#include <forge/animation_components.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

namespace forge {
inline constexpr unsigned animation_graph_runtime_profile_version = 1;
inline constexpr std::size_t animation_graph_byte_limit = 256 * 1024;
inline constexpr std::size_t animation_graph_cooked_byte_limit = animation_graph_byte_limit + 128;
inline constexpr std::size_t animation_graph_node_limit = 32;
inline constexpr std::size_t animation_graph_clip_limit = 8;
inline constexpr std::size_t animation_graph_input_limit = 4;
inline constexpr std::size_t animation_graph_joint_limit = 256;
inline constexpr std::uint32_t animation_graph_no_node = UINT32_MAX;

enum class AnimationGraphNodeKind { Clip, Blend, Additive };
struct AnimationGraphInput {
    // Scoped stable slot, not a position in the authored array. Names are labels.
    std::uint32_t slot = 0;
    std::string name;
    double initial = 0;
};
struct AnimationGraphWeight {
    // Slot zero selects the constant. Other slots use clamp(input * scale + bias).
    std::uint32_t input = 0;
    double constant = 0, scale = 1, bias = 0;
    double evaluate(std::span<const double, animation_graph_input_limit>) const;
};
struct AnimationGraphNode {
    // Local graph identity; never a catalog asset or native resource identity.
    AssetId id;
    std::string name;
    AnimationGraphNodeKind kind = AnimationGraphNodeKind::Clip;
    AssetRef<AnimationClipAsset> clip;
    AssetRef<AnimationMaskAsset> mask;
    std::uint32_t first = animation_graph_no_node, second = animation_graph_no_node;
    AnimationGraphWeight weight;
};
struct AnimationGraph {
    AssetId asset, skeleton;
    double cycle_seconds = 1;
    std::vector<AnimationGraphInput> inputs;
    // Derived dependency order. Shared subgraphs appear once, before their users.
    std::vector<AnimationGraphNode> nodes;
    std::uint32_t output = animation_graph_no_node;
    std::array<double, animation_graph_input_limit> initial_inputs() const;
    std::size_t resident_bytes() const;
};
struct AnimationGraphSource {
    nlohmann::json document;
    static AnimationGraphSource create(AssetId);
    static AnimationGraphSource parse(std::span<const std::byte>);
    static AnimationGraphSource parse_cooked(std::span<const std::byte>);
    AssetId asset() const;
    // Incomplete drafts may have null bindings or disconnected nodes. Present
    // links must resolve and even disconnected draft subgraphs must be acyclic.
    void validate() const;
    AnimationGraph compiled() const;
    std::vector<std::byte> cook() const;
};
AnimationGraph decode_animation_graph(std::span<const std::byte>, AssetId expected);
} // namespace forge
