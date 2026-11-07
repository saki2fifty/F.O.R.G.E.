#include <algorithm>
#include <cmath>
#include <forge/sound_cue.hpp>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>

namespace forge {
namespace {
std::uint32_t integer(const nlohmann::json& value) {
    if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
        (value.is_number_integer() && value.get<std::int64_t>() < 0) ||
        value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Sound Cue values require unsigned 32-bit integers");
    return value.get<std::uint32_t>();
}
// Fixed unsigned arithmetic gives reproducible choices without a global random owner.
std::uint32_t choice_bits(std::uint32_t seed, std::size_t node, std::uint64_t visit) {
    std::seed_seq input{seed, std::uint32_t(node), std::uint32_t(visit),
                        std::uint32_t(visit >> 32)};
    std::mt19937 generator(input);
    return generator();
}
} // namespace
SoundCueProgram SoundCueProgram::compile(const SoundCue& cue) {
    if (cue.nodes.size() > sound_cue_node_limit ||
        (cue.nodes.empty() ? cue.root != 0 : cue.root >= cue.nodes.size()))
        throw std::runtime_error("Sound Cue requires at most 32 nodes and an existing root");
    SoundCueProgram result;
    for (const auto& node : cue.nodes) {
        if (node.kind > 3 || bool(node.source.scene) != bool(node.source.entity) ||
            !std::isfinite(node.delay_seconds) || node.delay_seconds < 0 ||
            node.delay_seconds > sound_cue_delay_limit_seconds ||
            (node.kind != 0 && node.delay_seconds != 0))
            throw std::runtime_error(
                "Sound Cue requires valid kinds/references and Source-only delays of 0..10 seconds");
        if (node.kind == 0) {
            if (!node.children.empty())
                throw std::runtime_error("Sound Cue Source nodes cannot have children");
        } else if (node.source.entity || node.children.empty() ||
                   node.children.size() > sound_cue_child_limit)
            throw std::runtime_error(
                "Sound Cue branches require one to eight children and no source");
        for (const auto& edge : node.children)
            if (edge.target >= cue.nodes.size() || edge.weight < 1 || edge.weight > 65535 ||
                (node.kind != 2 && edge.weight != 1))
                throw std::runtime_error(
                    "Sound Cue child/weight is invalid; only weighted choices use weights1..65535");
    }
    result.cue_ = cue;
    std::array<unsigned char, sound_cue_node_limit> marks{};
    std::function<std::size_t(std::size_t)> visit = [&](std::size_t index) -> std::size_t {
        if (marks[index] == 1)
            throw std::runtime_error("Sound Cue graphs cannot contain cycles");
        if (marks[index] == 2)
            return result.outputs_[index];
        marks[index] = 1;
        const auto& node = cue.nodes[index];
        std::size_t count = node.kind == 0 ? 1 : 0;
        for (const auto& edge : node.children) {
            const auto child = visit(edge.target);
            count = node.kind == 1 ? count + child : std::max(count, child);
            if (count > sound_cue_output_limit)
                throw std::runtime_error(
                    "Sound Cue layers can emit at most 16 one-shots per trigger");
        }
        marks[index] = 2;
        return result.outputs_[index] = count;
    };
    // Validate dormant/unselected nodes too; edits cannot hide a cyclic/bomb branch.
    for (std::size_t i = 0; i < cue.nodes.size(); ++i)
        visit(i);
    return result;
}
std::size_t SoundCueProgram::maximum_outputs() const {
    return cue_.nodes.empty() ? 0 : outputs_[cue_.root];
}
SoundCueSelection SoundCueProgram::select(const SoundCueState& state) const {
    if (!cue_.enabled || cue_.nodes.empty())
        throw std::runtime_error("Sound Cue is disabled or has no nodes");
    if (state.triggers == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Sound Cue trigger counter is exhausted");
    SoundCueSelection result{state, {}, {}};
    result.sources.reserve(maximum_outputs());
    result.delay_seconds.reserve(maximum_outputs());
    std::function<void(std::size_t)> emit = [&](std::size_t index) {
        const auto& node = cue_.nodes[index];
        auto& visits = result.next.visits[index];
        if (visits == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Sound Cue node counter is exhausted");
        const auto previous = visits++;
        if (node.kind == 0) {
            if (!node.source.entity)
                throw std::runtime_error("Sound Cue Source node requires an Audio Source");
            result.sources.push_back(node.source);
            result.delay_seconds.push_back(node.delay_seconds);
        } else if (node.kind == 1) {
            for (const auto& edge : node.children)
                emit(edge.target);
        } else if (node.kind == 3) {
            emit(node.children[previous % node.children.size()].target);
        } else {
            std::uint32_t sum = 0;
            for (const auto& edge : node.children)
                sum += edge.weight;
            auto ticket =
                std::uint32_t((std::uint64_t(choice_bits(cue_.seed, index, previous)) * sum) >> 32);
            for (const auto& edge : node.children) {
                if (ticket < edge.weight) {
                    emit(edge.target);
                    return;
                }
                ticket -= edge.weight;
            }
            throw std::logic_error("Sound Cue choice exceeded its admitted weight total");
        }
    };
    emit(cue_.root);
    ++result.next.triggers;
    return result;
}
SoundCue sound_cue_from_json(const nlohmann::json& value) {
    SoundCue cue;
    cue.enabled = value.at("enabled").get<bool>();
    cue.seed = integer(value.at("seed"));
    cue.root = integer(value.at("root"));
    const auto& nodes = value.at("nodes");
    if (!nodes.is_array() || nodes.size() > sound_cue_node_limit)
        throw std::runtime_error("Sound Cue nodes require an array of at most 32 entries");
    for (const auto& entry : nodes) {
        SoundCueNode node;
        node.kind = integer(entry.at("kind"));
        node.delay_seconds = entry.value("delay_seconds", 0.);
        if (!entry.at("source").is_null())
            node.source = entry.at("source").get<EntityRef>();
        const auto& children = entry.at("children");
        if (!children.is_array() || children.size() > sound_cue_child_limit)
            throw std::runtime_error(
                "Sound Cue children require an array of at most eight entries");
        for (const auto& edge : children)
            node.children.push_back({integer(edge.at("target")), integer(edge.at("weight"))});
        cue.nodes.push_back(std::move(node));
    }
    (void)SoundCueProgram::compile(cue);
    return cue;
}
nlohmann::json sound_cue_json(const SoundCue& cue) {
    auto nodes = nlohmann::json::array();
    for (const auto& node : cue.nodes) {
        auto edges = nlohmann::json::array();
        for (const auto& edge : node.children)
            edges.push_back({{"target", edge.target}, {"weight", edge.weight}});
        nodes.push_back(
            {{"kind", node.kind},
             {"source", node.source.entity ? nlohmann::json(node.source) : nlohmann::json()},
             {"delay_seconds", node.delay_seconds},
             {"children", std::move(edges)}});
    }
    return {{"enabled", cue.enabled},
            {"seed", cue.seed},
            {"root", cue.root},
            {"nodes", std::move(nodes)}};
}
} // namespace forge
