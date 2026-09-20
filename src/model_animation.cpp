#include "model_animation.hpp"
#include "animation_asset.hpp"
#include "morph_animation.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::size_t index(const Json& j, std::size_t limit) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Invalid animation plan index");
    const auto n = j.get<std::uint64_t>();
    require(n < limit, "Animation plan index exceeds bounds");
    return static_cast<std::size_t>(n);
}
} // namespace
void validate_model_animation(const Json& meta, std::span<const ArtifactFile> files,
                              std::stop_token stop) {
    using namespace animation_detail;
    const auto cancelled = [&] {
        require(!stop.stop_requested(), "Model animation validation cancelled");
    };
    cancelled();
    require(meta.at("version") == 1, "Unsupported model animation plan");
    const auto& joints = meta.at("joint_nodes");
    const auto& nodes = meta.at("nodes");
    const auto& parents = meta.at("joint_parents");
    const auto& rests = meta.at("joint_rest_models");
    const auto& clips = meta.at("clips");
    require(joints.is_array() && !joints.empty() && joints.size() <= max_joints &&
                nodes.is_array() && nodes.size() == joints.size() && parents.is_array() &&
                parents.size() == joints.size() && rests.is_array() &&
                rests.size() == joints.size() && clips.is_array() && clips.size() <= 64 &&
                files.size() == clips.size() + 1,
            "Invalid model animation plan counts");
    std::set<std::size_t> node_set, joint_set;
    for (const auto& node : nodes)
        require(node_set.insert(index(node, 100000)).second, "Duplicate model rig node");
    std::vector<std::string> names;
    std::vector<std::int16_t> expected_parents;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const auto node = index(joints[i], 100000);
        require(node_set.contains(node) && joint_set.insert(node).second,
                "Invalid model joint mapping");
        names.push_back("forge_joint_" + std::to_string(node));
        const auto& p = parents[i];
        if (p == -1)
            expected_parents.push_back(-1);
        else
            expected_parents.push_back(static_cast<std::int16_t>(index(p, i)));
        require(rests[i].is_array() && rests[i].size() == 16, "Invalid model rest matrix");
        for (const auto& x : rests[i])
            require(x.is_number() && std::isfinite(x.get<double>()),
                    "Nonfinite expected model rest matrix");
    }
    require(valid_content_digest(meta.at("content_evidence").get<std::string>()) &&
                valid_content_digest(meta.at("semantic_evidence").get<std::string>()),
            "Invalid rig identity evidence");
    const auto& skins = meta.at("skins");
    const auto& node_skins = meta.at("node_skins");
    require(skins.is_array() && skins.size() <= 32768 && node_skins.is_array() &&
                node_skins.size() <= 100000,
            "Model skin binding counts exceed profile");
    for (const auto node : node_set)
        require(node < node_skins.size(), "Rig node exceeds model source nodes");
    for (const auto& skin : node_skins)
        if (!skin.is_null())
            (void)index(skin, skins.size());
    std::size_t skin_joint_budget = 32768;
    for (const auto& skin : skins) {
        const auto& mapped = skin.at("joints");
        const auto& inverse = skin.at("inverse_bind_matrices");
        require(mapped.is_array() && !mapped.empty() && mapped.size() <= joints.size() &&
                    mapped.size() <= skin_joint_budget && inverse.is_array() &&
                    inverse.size() == mapped.size(),
                "Invalid model skin joint/inverse-bind counts");
        skin_joint_budget -= mapped.size();
        std::set<std::size_t> unique;
        for (const auto& joint : mapped)
            require(unique.insert(index(joint, joints.size())).second,
                    "Duplicate model skin joint");
        for (const auto& matrix : inverse) {
            require(matrix.is_array() && matrix.size() == 16, "Invalid model inverse bind matrix");
            for (const auto& value : matrix)
                require(
                    value.is_number() && std::isfinite(value.get<double>()) &&
                        std::abs(value.get<double>()) <= std::numeric_limits<float>::max() &&
                        (value.get<double>() == 0 || static_cast<float>(value.get<double>()) != 0),
                    "Model inverse bind value cannot be stored as finite float");
            require(matrix[3] == 0 && matrix[7] == 0 && matrix[11] == 0 && matrix[15] == 1,
                    "Model inverse bind is not affine");
        }
    }
    std::map<std::string, std::span<const std::byte>> available;
    std::size_t budget = 256 * 1024 * 1024;
    for (const auto& file : files) {
        require(file.bytes.size() <= max_archive_bytes && file.bytes.size() <= budget &&
                    available.emplace(file.name, file.bytes).second,
                "Model animation output exceeds bounds or has duplicate file");
        budget -= file.bytes.size();
    }
    auto take = [&](const std::string& name) {
        const auto found = available.find(name);
        require(found != available.end(), "Missing model animation archive");
        const auto bytes = found->second;
        available.erase(found);
        return bytes;
    };
    auto skeleton = std::make_shared<Skeleton>(take("skeleton.ozz"));
    require(skeleton->joint_names() == names && skeleton->info().parents == expected_parents,
            "Converted skeleton joint order or parent mapping differs from source plan");
    const auto actual = skeleton->rest_pose();
    for (std::size_t i = 0; i < actual.size(); ++i) {
        // Relative to the full linear column or translation, so harmless
        // float composition error near zero is not compared against zero alone.
        for (unsigned column = 0; column < 4; ++column) {
            double magnitude = 1;
            for (unsigned r = 0; r < 4; ++r)
                magnitude = std::max(magnitude, std::abs(rests[i][column * 4 + r].get<double>()));
            for (unsigned r = 0; r < 4; ++r)
                require(std::abs(actual[i][column * 4 + r] -
                                 rests[i][column * 4 + r].get<double>()) <= magnitude * 5e-5,
                        "Converted skeleton rest pose differs from admitted source");
        }
    }
    for (std::size_t i = 0; i < clips.size(); ++i) {
        cancelled();
        const auto& entry = clips[i];
        const auto filename = "clip-" + std::to_string(i) + ".ozz";
        require(entry.at("file") == filename && index(entry.at("source_index"), clips.size()) == i,
                "Invalid model animation clip mapping");
        require(valid_content_digest(entry.at("content_evidence").get<std::string>()) &&
                    valid_content_digest(entry.at("semantic_evidence").get<std::string>()),
                "Invalid clip identity evidence");
        const auto duration = entry.at("duration").get<double>();
        require(std::isfinite(duration) && duration >= double(.0001f) && duration <= 3600,
                "Invalid model clip duration");
        auto clip = std::make_shared<Clip>(take(filename));
        require(clip->info().tracks == skeleton->info().tracks &&
                    std::abs(clip->info().duration - duration) <= 1e-6 * std::max(1., duration),
                "Converted clip tracks or duration differ from source plan");
        MorphAnimation morphs(entry.at("morph_tracks"));
        for (const auto& track : entry.at("morph_tracks")) {
            require(node_set.contains(index(track.at("node"), 100000)),
                    "Morph track targets a node outside its admitted rig");
            require(track.at("times").back().get<double>() <= duration,
                    "Morph track exceeds clip duration");
        }
        Sampler sampler(skeleton, clip);
        for (float ratio : {0.f, .25f, .5f, .75f, 1.f}) {
            (void)sampler.sample(ratio);
            (void)morphs.sample(duration * ratio);
        }
    }
    require(available.empty(), "Unexpected model animation output");
    cancelled();
}
} // namespace forge::asset_detail
